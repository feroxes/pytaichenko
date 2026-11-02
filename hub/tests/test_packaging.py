import http.server
import json
import re
import subprocess
import sys
import threading
from pathlib import Path

import pytest
import yaml

HUB_DIR = Path(__file__).resolve().parents[1]
ROOT = HUB_DIR.parent
NOT_SHIPPED = ("devtools", "samples", "tests")


def instructions(text: str) -> list[tuple[str, str]]:
    joined = re.sub(r"\\\n\s*", " ", text)
    found = []
    for line in joined.splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            keyword, _, rest = line.partition(" ")
            found.append((keyword.upper(), rest.strip()))
    return found


@pytest.fixture(scope="module")
def dockerfile() -> list[tuple[str, str]]:
    return instructions((HUB_DIR / "Dockerfile").read_text(encoding="utf-8"))


@pytest.fixture(scope="module")
def compose() -> dict:
    return yaml.safe_load((ROOT / "compose.yaml").read_text(encoding="utf-8"))


def of(found: list[tuple[str, str]], keyword: str) -> list[str]:
    return [rest for key, rest in found if key == keyword]


def copied(found: list[tuple[str, str]]) -> list[str]:
    sources = []
    for rest in of(found, "COPY") + of(found, "ADD"):
        words = [w for w in rest.split() if not w.startswith("--")]
        sources.extend(words[:-1])
    return sources


def test_the_image_starts_from_slim_python(dockerfile):
    assert of(dockerfile, "FROM") == ["python:3.12-slim"]


def test_the_image_copies_the_package_and_nothing_for_developers(dockerfile):
    sources = copied(dockerfile)
    assert {"pyproject.toml", "src"} <= set(sources)
    for source in sources:
        assert Path(source).parts[0] not in NOT_SHIPPED, source


def test_the_build_context_leaves_out_developer_files():
    ignored = {line.strip() for line in (HUB_DIR / ".dockerignore").read_text(encoding="utf-8").splitlines()}
    assert {".venv", "devtools", "samples", "tests"} <= ignored


def test_the_build_context_leaves_out_python_leftovers_at_any_depth():
    ignored = {line.strip() for line in (HUB_DIR / ".dockerignore").read_text(encoding="utf-8").splitlines()}
    assert {"**/__pycache__", "**/*.egg-info", "**/*.pyc"} <= ignored
    assert not {"__pycache__", "*.egg-info", "*.pyc"} & ignored


def test_the_image_builds_without_buildkit_only_flags(dockerfile):
    for keyword in ("COPY", "ADD"):
        for rest in of(dockerfile, keyword):
            assert "--chmod" not in rest and "--chown" not in rest, rest
    runs = of(dockerfile, "RUN")
    assert any(re.fullmatch(r"chmod 0?755 /usr/local/bin/docker-entrypoint\.sh", run) for run in runs)


def test_the_package_is_installed_from_the_copied_sources(dockerfile):
    runs = " ".join(of(dockerfile, "RUN"))
    assert re.search(r"pip install\b[^&;]*\s/build\b", runs)
    assert "-e " not in runs and "requirements" not in runs


def test_the_service_runs_as_an_unprivileged_user(dockerfile):
    runs = " ".join(of(dockerfile, "RUN"))
    assert "useradd" in runs and "voice-hub" in runs
    script = (HUB_DIR / "docker-entrypoint.sh").read_text(encoding="utf-8")
    assert "setpriv" in script and "--reuid=voice-hub" in script and "exec " in script
    assert of(dockerfile, "ENTRYPOINT") == ['["docker-entrypoint.sh"]']
    assert "docker-entrypoint.sh" in copied(dockerfile)


def test_the_data_folder_is_a_volume_and_the_command_uses_it(dockerfile):
    assert of(dockerfile, "VOLUME") == ["/data"]
    assert json.loads(of(dockerfile, "CMD")[0]) == ["python", "-m", "voice_hub", "--data", "/data"]


def test_the_health_check_asks_whether_the_service_answers_not_whether_speakers_are_online(dockerfile):
    (check,) = of(dockerfile, "HEALTHCHECK")
    command = json.loads(check.split(" CMD ", 1)[1])
    assert command[:2] == ["python", "-c"]
    script = command[2]
    assert "127.0.0.1" in script and "urlopen" in script
    assert "/health" not in script
    compile(script, "healthcheck", "exec")


class Answers(http.server.BaseHTTPRequestHandler):
    def do_GET(self) -> None:
        self.send_response(200)
        self.end_headers()

    def log_message(self, *args) -> None:
        pass


def health_check(dockerfile, env: dict[str, str], cwd: Path) -> int:
    (check,) = of(dockerfile, "HEALTHCHECK")
    script = json.loads(check.split(" CMD ", 1)[1])[2]
    return subprocess.run([sys.executable, "-c", script], env=env, cwd=cwd, capture_output=True, timeout=30).returncode


def test_the_health_check_asks_the_port_from_the_environment_first(dockerfile, tmp_path):
    with http.server.ThreadingHTTPServer(("127.0.0.1", 0), Answers) as server:
        threading.Thread(target=server.serve_forever, daemon=True).start()
        port = server.server_address[1]
        assert health_check(dockerfile, {"VOICE_HUB_PORT": f" {port} "}, tmp_path) == 0
        server.shutdown()
    assert health_check(dockerfile, {"VOICE_HUB_PORT": str(port)}, tmp_path) != 0


def test_the_entrypoint_script_is_valid_shell():
    path = HUB_DIR / "docker-entrypoint.sh"
    assert b"\r" not in path.read_bytes()
    subprocess.run(["sh", "-n", str(path)], check=True)


def fake_tools(folder: Path, uid: int, chown_status: int) -> dict[str, str]:
    folder.mkdir()
    tools = {
        "id": f"#!/bin/sh\necho {uid}\n",
        "chown": f"#!/bin/sh\necho \"chown: changing ownership of '/data': Read-only file system\" >&2\nexit {chown_status}\n"
        if chown_status else "#!/bin/sh\nexit 0\n",
        "setpriv": "#!/bin/sh\necho \"setpriv $*\"\n",
    }
    for name, text in tools.items():
        tool = folder / name
        tool.write_text(text, encoding="utf-8")
        tool.chmod(0o755)
    return {"PATH": f"{folder}:/usr/bin:/bin"}


def entrypoint(env: dict[str, str]) -> subprocess.CompletedProcess:
    return subprocess.run(["sh", str(HUB_DIR / "docker-entrypoint.sh"), "python", "-m", "voice_hub"], env=env,
                          capture_output=True, text=True, timeout=10)


def test_the_entrypoint_hands_the_data_folder_over_and_drops_root(tmp_path):
    done = entrypoint(fake_tools(tmp_path / "bin", 0, 0))
    assert done.returncode == 0, done.stderr
    assert done.stdout.strip() == "setpriv --reuid=voice-hub --regid=voice-hub --init-groups python -m voice_hub"


def test_the_entrypoint_explains_a_data_folder_it_cannot_take_over(tmp_path):
    done = entrypoint(fake_tools(tmp_path / "bin", 0, 1))
    assert done.returncode == 2
    assert "setpriv" not in done.stdout
    hint = done.stderr.strip().splitlines()[-1]
    assert hint.startswith("error: ") and "/data" in hint and "10001" in hint
    assert "read-only" in hint and "sudo chown -R 10001:10001 ./data" in hint
    assert "SELinux" in hint and "./data:/data:z" in hint


def test_the_entrypoint_runs_the_command_as_is_when_not_root(tmp_path):
    env = fake_tools(tmp_path / "bin", 1000, 1)
    done = subprocess.run(["sh", str(HUB_DIR / "docker-entrypoint.sh"), "echo", "plain"], env=env,
                          capture_output=True, text=True, timeout=10)
    assert done.returncode == 0 and done.stdout.strip() == "plain" and done.stderr == ""


def test_compose_builds_the_hub_on_the_host_network_with_a_data_volume(compose):
    service = compose["services"]["voice-hub"]
    assert service["build"] == "./hub"
    assert service["network_mode"] == "host"
    assert "./data:/data" in service["volumes"]
    assert service["restart"] == "unless-stopped"
    assert "ports" not in service


def test_compose_variables_are_optional(compose):
    environment = compose["services"]["voice-hub"]["environment"]
    assert set(environment) == {"OPENAI_API_KEY", "VOICE_HUB_PASSWORD", "VOICE_HUB_HOSTS", "VOICE_HUB_PORT"}
    for name, value in environment.items():
        assert re.fullmatch(r"\$\{" + name + r":-\}", value), name


def test_the_data_folder_and_env_file_stay_out_of_git():
    ignored = (ROOT / ".gitignore").read_text(encoding="utf-8").splitlines()
    assert "/data/" in ignored and ".env" in ignored


README_SECTIONS = {
    "README.md": ["What it does", "How it works", "Build it", "Versions and releases", "License", "Thanks"],
    "README.uk.md": ["Що вміє", "Як це працює", "Як зібрати", "Версії й випуски", "Ліцензія", "Подяки"],
}


@pytest.mark.parametrize(
    ("name", "lang", "intro_words", "foreign"),
    [
        ("README.md", "en", ("wake phrase", "OpenAI Realtime"), ("kitchen", "speaks Ukrainian", "revision", "prototype")),
        (
            "README.uk.md",
            "uk",
            ("ключову фразу", "OpenAI Realtime"),
            ("кухн", "власник вписує", "українською йде", "говорить українською", "ревізі", "прототип"),
        ),
    ],
)
def test_the_project_readme_describes_the_project_not_one_home(name, lang, intro_words, foreign):
    text = (ROOT / name).read_text(encoding="utf-8")
    intro = text.split("\n## ", 1)[0]
    for word in intro_words:
        assert word in intro, word
    for phrase in ("Home Assistant", "Джарвіс", *foreign):
        assert phrase.lower() not in text.lower(), phrase
    assert "Jarvis" not in text.replace("Hey Jarvis", ""), "Jarvis only as one of the wake phrases"
    if "Hey Jarvis" in text:
        assert "Okay Nabu" in text and "Hey Mycroft" in text, "the wake phrase is a choice of three"
    assert "!" not in text, "a calm tone, no exclamation marks"
    assert f"https://feroxes.github.io/pytaichenko/?lang={lang}" in intro, "the guide in the README's language"


@pytest.mark.parametrize("name", README_SECTIONS)
def test_the_project_readme_leaves_the_build_details_to_the_site(name):
    text = (ROOT / name).read_text(encoding="utf-8")
    for detail in ("GPIO", "| VDD", "M2.5", "```", "mermaid", "git tag", "pytest", "case/README", "firmware/README", "hub/README"):
        assert detail not in text, detail
    lang = "en" if name == "README.md" else "uk"
    for anchor in ("parts", "print", "solder", "assembly", "firmware", "server"):
        assert f"https://feroxes.github.io/pytaichenko/?lang={lang}#{anchor}" in text, anchor
    assert "https://github.com/feroxes/pytaichenko/releases" in text
    assert "2.0.0" in text and ".factory.bin" in text and "-stl.zip" in text


@pytest.mark.parametrize("name", README_SECTIONS)
def test_the_project_readme_shows_how_it_works_with_the_diagram_in_its_language(name):
    text = (ROOT / name).read_text(encoding="utf-8")
    lang = "en" if name == "README.md" else "uk"
    picture = re.search(r'<img src="(site/assets/how-it-works-(\w+)\.svg)" width="(\d+)" alt="([^"]+)">', text)
    assert picture, "the diagram is an <img> with a width and alt text"
    assert picture[2] == lang
    assert (ROOT / picture[1]).is_file()
    assert 700 <= int(picture[3]) <= 800
    assert len(picture[4]) > 60


def test_the_project_readmes_link_to_each_other_under_the_title():
    english = (ROOT / "README.md").read_text(encoding="utf-8").splitlines()
    ukrainian = (ROOT / "README.uk.md").read_text(encoding="utf-8").splitlines()
    assert english[0].startswith("# ") and ukrainian[0].startswith("# ")
    assert english[2] == "**English** · [Українська](README.uk.md)"
    assert ukrainian[2] == "[English](README.md) · **Українська**"
    for name, lines in (("README.md", english), ("README.uk.md", ukrainian)):
        assert [line[3:] for line in lines if line.startswith("## ")] == README_SECTIONS[name], name
        assert not [line for line in lines if line.startswith("### ")], f"{name} has no subsections"


def test_the_site_is_the_one_guide_no_readme_in_the_folders():
    for folder in ("case", "firmware", "hub"):
        assert not (ROOT / folder / "README.md").exists(), folder
