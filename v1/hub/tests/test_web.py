import dataclasses
import fnmatch
import json
import re
import shutil
import subprocess
import tomllib
from pathlib import Path

import pytest

from voice_hub.api import HTTP_CODES, SPEAKER_STATUS, Api
from voice_hub.app import PAGE_FILES, build_app
from voice_hub.config import Timing
from voice_hub.registry import DeviceRegistry, Hub
from voice_hub.settings import (DEFAULT_DEVICE_PORT, MIN_SERVER_PORT, MIN_STOP_GRACE_S, OpenAISettings, Settings,
                                 SettingsError, SettingsStore, apply_patch, check_address)
from voice_hub.streams import AudioStreams

HUB_DIR = Path(__file__).resolve().parents[1]
SOURCE = HUB_DIR / "src" / "voice_hub"
WEB_DIR = SOURCE / "web"
NODE = shutil.which("node")
TYPES = {
    "/": "text/html",
    "/app.js": "text/javascript",
    "/style.css": "text/css",
    "/favicon.svg": "image/svg+xml",
}
needs_node = pytest.mark.skipif(NODE is None, reason="node is not installed")


class NoDiscovery:
    def snapshot(self) -> list:
        return []


@pytest.fixture
async def page(tmp_path, aiohttp_client):
    store = SettingsStore(tmp_path / "settings.json")
    settings = Settings()
    store.save(settings)
    hub = Hub(store, settings, DeviceRegistry(lambda device: None))
    await hub.start()
    client = await aiohttp_client(build_app(AudioStreams(), hub.registry.health, Api(hub, NoDiscovery())))
    yield client
    await hub.close()


def page_logic(expression: str):
    script = (
        "const vm = require('node:vm'); const fs = require('node:fs');"
        "const store = new Map();"
        "const document = {readyState: 'loading', addEventListener() {},"
        " documentElement: {setAttribute() {}, getAttribute() { return null; }}};"
        "const window = {document, navigator: {languages: []},"
        " localStorage: {getItem: (k) => store.get(k) ?? null, setItem: (k, v) => store.set(k, v)}};"
        "window.window = window;"
        "vm.runInNewContext(fs.readFileSync(process.argv[1], 'utf8'), {window, document, navigator: window.navigator});"
        "const H = window.PYTAICHENKO_HUB;"
        f"process.stdout.write(JSON.stringify({expression}));"
    )
    done = subprocess.run([NODE, "-e", script, str(WEB_DIR / "app.js")], capture_output=True, text=True, timeout=30,
                          check=True)
    return json.loads(done.stdout)


async def test_the_page_and_its_files_are_served_with_their_types(page):
    for path, content_type in TYPES.items():
        resp = await page.get(path)
        assert resp.status == 200, path
        assert resp.headers["Content-Type"] == f"{content_type}; charset=utf-8", path
        name = "index.html" if path == "/" else path[1:]
        assert await resp.read() == (WEB_DIR / name).read_bytes(), path


async def test_the_page_tells_the_browser_to_trust_only_itself(page):
    for path in TYPES:
        resp = await page.get(path)
        policy = resp.headers["Content-Security-Policy"]
        for rule in ("default-src 'none'", "script-src 'self'", "style-src 'self'", "connect-src 'self'",
                     "frame-ancestors 'none'", "form-action 'none'", "base-uri 'none'"):
            assert rule in policy, (path, rule)
        assert resp.headers["X-Content-Type-Options"] == "nosniff"
        assert resp.headers["X-Frame-Options"] == "DENY"
        assert resp.headers["Referrer-Policy"] == "no-referrer"
        assert resp.headers["Cache-Control"] == "no-cache"


async def test_the_page_needs_no_login_and_opens_under_any_name(page):
    resp = await page.get("/", headers={"Host": "evil.test"})
    assert resp.status == 200
    assert "<html" in await resp.text()
    resp = await page.get("/api/state", headers={"Host": "evil.test"})
    assert resp.status == 403 and (await resp.json())["error"] == "foreign_host"


async def test_only_the_page_files_are_served(page):
    for path in ("/index.html", "/web/app.js", "/settings.json", "/api.py", "/app.py", "/../settings.py",
                 "/%2e%2e/settings.py", "/static/app.js", "/app.js/"):
        resp = await page.get(path)
        assert resp.status == 404, path


async def test_without_the_api_there_is_no_page(aiohttp_client):
    client = await aiohttp_client(build_app(AudioStreams(), dict))
    assert (await client.get("/")).status == 404
    assert (await client.get("/app.js")).status == 404


def test_every_page_file_is_listed_and_exists():
    served = {name for name, _ in PAGE_FILES.values()}
    on_disk = {path.name for path in WEB_DIR.iterdir() if path.is_file()}
    assert served == on_disk


def test_the_page_files_are_packaged():
    project = tomllib.loads((HUB_DIR / "pyproject.toml").read_text(encoding="utf-8"))
    patterns = project["tool"]["setuptools"]["package-data"]["voice_hub"]
    for path in WEB_DIR.iterdir():
        assert any(fnmatch.fnmatch(f"web/{path.name}", pattern) for pattern in patterns), path.name


def test_the_page_loads_nothing_from_other_sites():
    html = (WEB_DIR / "index.html").read_text(encoding="utf-8")
    css = (WEB_DIR / "style.css").read_text(encoding="utf-8")
    script = (WEB_DIR / "app.js").read_text(encoding="utf-8")
    for tag in re.findall(r"<(?:script|link|img|iframe|source|video|audio)\b[^>]*>", html):
        for url in re.findall(r'(?:src|href)="([^"]*)"', tag):
            assert "//" not in url and not url.startswith("/"), tag
    assert "@import" not in css and not re.search(r"url\(\s*['\"]?(?:https?:)?//", css)
    assert not re.search(r"fetch\(\s*['\"](?:https?:)?//", script)
    assert "innerHTML" not in script and "insertAdjacentHTML" not in script and "eval(" not in script
    assert " style=" not in html and "<style" not in html and "<script>" not in html
    for text in (html, css, script):
        assert "Home Assistant" not in text
        assert not re.search(r"jarvis|джарвіс", text, re.IGNORECASE)


def test_the_page_never_puts_a_secret_in_a_form_url():
    html = (WEB_DIR / "index.html").read_text(encoding="utf-8")
    forms = re.findall(r"<form\b[^>]*>", html)
    assert forms
    for form in forms:
        assert 'method="post"' in form, form
        assert "action=" not in form, form
    for field in re.findall(r"<input\b[^>]*>", html):
        if re.search(r'data-secret|type="password"', field):
            assert 'type="password"' in field, field
            assert "value=" not in field, field


def test_password_managers_are_told_not_to_fill_the_key_fields():
    html = (WEB_DIR / "index.html").read_text(encoding="utf-8")
    secrets = [field for field in re.findall(r"<input\b[^>]*>", html) if "data-secret" in field]
    assert len(secrets) == 2
    for field in secrets:
        assert 'autocomplete="off"' in field, field
        assert "data-1p-ignore" in field and 'data-lpignore="true"' in field, field
        assert "data-bwignore" in field and 'data-form-type="other"' in field, field


@needs_node
def test_the_model_and_the_voice_are_real_lists():
    html = (WEB_DIR / "index.html").read_text(encoding="utf-8")
    assert "<datalist" not in html
    for name, options in (
        ("model", ("gpt-realtime-2.1-mini", "gpt-realtime-2.1")),
        ("voice", ("marin", "cedar", "alloy", "ash", "ballad", "coral", "echo", "sage", "shimmer", "verse")),
    ):
        block = re.search(rf'<select id="openai-{name}".*?</select>', html, re.S)
        assert block, name
        found = re.findall(r'<option value="([^"]+)">', block.group(0))
        assert tuple(found) == options, name


def test_select_arrows_keep_away_from_the_edge_and_auth_forms_are_centred():
    css = (WEB_DIR / "style.css").read_text(encoding="utf-8")
    select = re.search(r"\nselect \{(.*?)\n\}", css, re.S).group(1)
    assert "appearance: none" in select
    assert "padding-right: 40px" in select
    narrow = re.search(r"\.screen\.narrow \{(.*?)\}", css, re.S).group(1)
    assert "margin-left: auto" in narrow and "margin-right: auto" in narrow


def test_the_page_logic_passes_its_own_tests():
    files = sorted(str(path) for path in (HUB_DIR / "tests" / "web").glob("*.test.mjs"))
    assert len(files) >= 2
    done = subprocess.run([NODE, "--test", *files], cwd=HUB_DIR, capture_output=True, text=True, timeout=120)
    assert done.returncode == 0, done.stdout[-4000:] + done.stderr[-4000:]


@needs_node
def test_the_page_shows_every_timing_with_the_service_defaults():
    shown = page_logic("H.TIMING_FIELDS.map((f) => [f.name, f.kind])")
    defaults = page_logic("H.DEFAULTS")
    names = [name for name, _ in shown]
    assert sorted(names) == sorted(f.name for f in dataclasses.fields(Timing))
    assert len(names) == len(set(names))
    kinds = dict(shown)
    assert kinds["max_follow_ups"] == "count"
    assert kinds["mic_holdoff_s"] == "seconds0"
    assert kinds["mic_stop_grace_s"] == "grace"
    assert page_logic("H.MIN_STOP_GRACE") == MIN_STOP_GRACE_S
    assert {kind for name, kind in shown if name not in {"max_follow_ups", "mic_holdoff_s", "mic_stop_grace_s"}} \
        == {"seconds"}
    assert defaults["timing"] == dataclasses.asdict(Timing())
    openai = OpenAISettings()
    assert defaults["openai"] == {"vad_threshold": openai.vad_threshold, "silence_ms": openai.silence_ms}
    assert defaults["http_port"] == Settings().http_port
    assert defaults["port"] == DEFAULT_DEVICE_PORT


HOSTS = ["192.0.2.10", " 192.0.2.10 ", "hub", "hub.lan", "hub.lan.", "hub..lan", ".hub", "my_hub-2.home.arpa",
         "192.0.2.300", "x" * 63, "x" * 64, "a." * 126 + "a", "a." * 127 + "a", "fe80::1", "fe80::1%eth0", "fe80::1%",
         "fe80::1%a%b", "fe80::1%eth0/1", "::", "::1", "1::", ":1::2", "1::2:", ":::1", "1::2::3", "1:2:3:4:5:6:7:8",
         "1:2:3:4:5:6:7:8:9", "1:2:3:4:5:6:7::", ":1:2:3:4:5:6:7", "1:2:3:4:5:6:7:", "::ffff:192.0.2.1",
         "::ffff:192.0.2.01", "::ffff:192.0.2.256", "::1.2.3", "1:2:3:4:5:6:192.0.2.1", "1:2:3:4:5:6:7:192.0.2.1",
         "12345::1", "00001::", "01:02::", "g::1", "::FFFF", "[::1]", "::1/64", "http://192.0.2.10",
         "192.0.2.10:8080", "http://192.0.2.10:8080/", "hub.lan/audio", "hub:8080", "hub lan", "user@hub", "hub?x",
         "хаб.lan", "", "192.168.1", "1.2.3", "192.168.010.20", "192.0.2.10.", "12345", "hub.123", "123.lan",
         "4u.home", "fe80::1%a b", "fe80::1%eth0 x", "fe80::1%en0.1", "fe80::1%wlan_0-1", "fe80::1%7",
         "fe80::1%eth0:1", "192.0.2.10%eth0", "::ffff:192.0.2.1%eth0"]


def _service_takes_host(host: str) -> bool:
    try:
        check_address(host, DEFAULT_DEVICE_PORT)
    except SettingsError:
        return False
    return True


def _service_takes_server_address(host: str) -> bool:
    try:
        apply_patch(Settings(), {"advertise_host": host})
    except SettingsError:
        return False
    return True


@needs_node
def test_the_page_checks_hosts_like_the_service():
    taken = page_logic(f"{json.dumps(HOSTS)}.map((host) => H.isHost(host))")
    assert dict(zip(HOSTS, taken)) == {host: _service_takes_host(host) for host in HOSTS}


@needs_node
def test_the_page_checks_the_server_address_like_the_service():
    hosts = [host for host in HOSTS if host.strip()]
    expression = (f"{json.dumps(hosts)}.map((host) => H.buildPatch(H.SPECS.server, "
                  "{http_port: 8080, advertise_host: null}, {advertise_host: host}).errors.length === 0)")
    taken = page_logic(expression)
    assert dict(zip(hosts, taken)) == {host: _service_takes_server_address(host) for host in hosts}
    assert not _service_takes_server_address("fe80::1%eth0") and _service_takes_host("fe80::1%eth0")


@needs_node
def test_the_page_checks_the_server_port_like_the_service():
    low, high = MIN_SERVER_PORT, 65535
    cases = [str(n) for n in (low - 1, low, 8080, high, high + 1)]
    taken = page_logic(f"{json.dumps(cases)}.map((port) => !H.checkValue('server_port', port).rule)")
    assert taken == [False, True, True, True, False]
    assert page_logic("H.checkValue('server_port', '1').rule.vars") == {"low": low, "high": high}


@needs_node
def test_the_page_explains_every_error_the_api_can_send():
    source = (SOURCE / "api.py").read_text(encoding="utf-8")
    codes = set(re.findall(r'ApiError\(\d+, "([a-z_]+)"', source))
    codes |= set(re.findall(r'_failure\(\d+, "([a-z_]+)"', source))
    codes |= set(SPEAKER_STATUS) | set(HTTP_CODES.values())
    for module in ("adopt.py", "device.py", "registry.py"):
        text = (SOURCE / module).read_text(encoding="utf-8")
        codes |= set(re.findall(r'(?:AdoptError|WakeWordError|VolumeError)\(f?"[^"]*", "([a-z_]+)"\)', text))
    assert {"foreign_host", "login_required", "has_key", "no_key_yet", "offline", "save_failed"} <= codes
    known = set(page_logic("H.ERROR_CODES"))
    assert codes <= known, sorted(codes - known)
    texts = page_logic("H.TEXT")
    for code in codes:
        for lang in ("uk", "en"):
            text = texts[lang].get(f"err.{code}", "")
            assert len(text.split()) >= 4 and ("_" not in code or code not in text), (lang, code)
