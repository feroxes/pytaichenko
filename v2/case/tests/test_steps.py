import json
import re
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor
from functools import lru_cache
from pathlib import Path

import pytest

from conftest import BACKEND, CASE_DIR, OPENSCAD, SCAD_FILE

STEPS_FILE = CASE_DIR / "steps.scad"
HERO_FILE = CASE_DIR / "hero.scad"
SCRIPT = CASE_DIR / "render_steps.sh"
README = CASE_DIR / "README.md"
WORKERS = 6

ASSEMBLY_CALLS = {
    "dummy_speaker()": {"speaker"},
    "dummy_devkit()": {"devkit"},
    "dummy_antenna()": {"antenna"},
    "dummy_usb_plug(com_plug_w, com_plug_h)": {"com_plug"},
    "dummy_amp()": {"amp"},
    "dummy_cap()": {"cap"},
    "dummy_mic(mic_az_l)": {"mic_l"},
    "dummy_mic(mic_az_r)": {"mic_r"},
    "dummy_button()": {"button"},
    "dummy_filament()": {"filament"},
    "dummy_filament_tails()": {"filament_tails"},
    "dummy_transistor()": {"transistor"},
    "dummy_speaker_screws()": {"speaker_screws"},
    "dummy_metal_washers()": {"metal_washers"},
    "dummy_inserts()": {"spk_inserts", "column_inserts", "tray_inserts"},
    "dummy_top_screws()": {"top_screws"},
    "body()": {"body"},
    "ring_arc()": {"ring_arc"},
    "dummy_top_gasket()": {"top_gasket"},
    "speaker_gasket()": {"speaker_gasket"},
    "top()": {"top"},
    "top_plugs()": {"top_plugs"},
    "button_cap()": {"button_cap"},
    "tray()": {"tray"},
    "com_bushing()": {"com_bushing"},
    "dummy_tray_gasket()": {"tray_gasket"},
    "dummy_tray_screws()": {"tray_screws"},
    "dummy_feet()": {"feet"},
}

STAGES = ["Tray", "Body onto the tray", "Lid"]


def _run(path, out, defines=(), extra=()):
    cmd = [OPENSCAD, *BACKEND, *defines, "-o", str(out), *extra, str(path)]
    return subprocess.run(cmd, capture_output=True, text=True)


def _problems(proc, text=""):
    lines = proc.stderr.splitlines() + text.splitlines()
    return [line for line in lines if re.search(r"WARNING|ERROR|Assertion", line)]


def _echo_values(text):
    values = {}
    for line in text.splitlines():
        m = re.match(r'ECHO: (\w+) = (.*)$', line)
        if m:
            values[m.group(1)] = json.loads(m.group(2))
    return values


@lru_cache(maxsize=None)
def _echo(step):
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "step.echo"
        proc = _run(STEPS_FILE, out, ["-D", f"step={step}"], ["--export-format", "echo"])
        text = out.read_text() if out.exists() else ""
    return proc.returncode, _problems(proc, text), _echo_values(text)


def cameras():
    script = SCRIPT.read_text()
    return re.findall(r'^\s+"(-?[\d.]+(?:,-?[\d.]+){6})"$', script, re.M)


def step_count():
    return int(_echo(1)[2]["steps"])


@lru_cache(maxsize=None)
def all_echoes():
    n = step_count()
    with ThreadPoolExecutor(WORKERS) as pool:
        return dict(zip(range(1, n + 1), pool.map(_echo, range(1, n + 1))))


def readme_steps():
    text = README.read_text()
    m = re.search(r"^## Assembly steps\n(.*?)(?=^## |\Z)", text, re.M | re.S)
    assert m, "README has no Assembly steps section"
    rows = re.findall(r"^\| (\d+) \| ([^|]+?) \| ([^|]+?) \|$", m.group(1), re.M)
    count = re.search(r"(\d+) steps in three stages", m.group(1))
    return int(count.group(1)) if count else None, [(int(a), b, c) for a, b, c in rows]


def assembly_calls():
    text = SCAD_FILE.read_text()
    body = re.search(r"module assembly\(\) \{\n(.*?)\n\}", text, re.S).group(1)
    return [re.sub(r"^\s*color\([^)]*\)\s*", "", line).rstrip(";").strip() for line in body.splitlines() if line.strip()]


def test_the_files_exist_and_have_no_comments():
    for f in (STEPS_FILE, HERO_FILE, SCRIPT):
        assert f.exists(), f.name
        assert not re.search(r"//|/\*|^\s*#(?!!)", f.read_text(), re.M), f"comment in {f.name}"


def test_every_step_evaluates_without_warnings():
    for step, (code, problems, values) in all_echoes().items():
        assert code == 0, f"step {step} exit {code}"
        assert not problems, f"step {step}: {problems}"
        assert values["step"] == step


def test_an_unknown_step_is_refused():
    code, problems, _ = _echo(step_count() + 1)
    assert code != 0 or problems


def test_one_camera_per_step():
    assert len(cameras()) == step_count()


def test_steps_count_is_in_range_and_each_stage_fits_on_a_phone():
    n = step_count()
    assert 18 <= n <= 27
    stages = [all_echoes()[k][2]["stage"] for k in range(1, n + 1)]
    assert stages == sorted(stages)
    assert sorted(set(stages)) == [1, 2, 3]
    for s in (1, 2, 3):
        assert stages.count(s) <= 11


def test_parts_only_grow_and_every_step_does_something():
    echoes = all_echoes()
    prev = set(echoes[1][2]["parts"]) - set(echoes[1][2]["new"])
    for step in sorted(echoes):
        v = echoes[step][2]
        parts = set(v["parts"])
        assert len(parts) == len(v["parts"]), f"step {step} lists a part twice"
        assert prev <= parts, f"step {step} drops {prev - parts}"
        assert set(v["new"]) == parts - prev, f"step {step} new parts"
        assert v["new"] or v["moving"], f"step {step} adds nothing and moves nothing"
        prev = parts


def test_assembly_calls_are_all_mapped():
    assert sorted(assembly_calls()) == sorted(ASSEMBLY_CALLS)


def test_final_step_is_the_full_assembly():
    echoes = all_echoes()
    final = {p for p in echoes[max(echoes)][2]["parts"] if not p.startswith("glue_")}
    assert final == set().union(*ASSEMBLY_CALLS.values())


def test_readme_lists_every_step_with_its_stage():
    n = step_count()
    count, rows = readme_steps()
    assert count == n
    assert [r[0] for r in rows] == list(range(1, n + 1))
    echoes = all_echoes()
    for k, stage, caption in rows:
        assert stage == STAGES[echoes[k][2]["stage"] - 1], f"step {k} stage"
        assert caption.endswith("."), f"step {k} caption"
        assert "!" not in caption


def test_script_renders_into_its_own_folder_and_cleans_only_its_files():
    script = SCRIPT.read_text()
    assert script.startswith("#!/usr/bin/env bash")
    assert "--backend=manifold" in script
    assert "--render" in script
    assert "--colorscheme=Tomorrow" in script
    assert "--imgsize=1000,750" in script
    assert "--imgsize=1200,900" in script
    assert 'OUT="../../site/assets/renders/v2"' in script
    assert re.search(r'rm -f "\$OUT"/assembly-\*\.png', script)
    assert len(re.findall(r"\brm\b", script)) == 1
    assert "hero.scad" in script and "hero.png" in script
    assert not re.search(r"/Users/|/home/[a-z]|/private/tmp", script)


def _render(step):
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "step.png"
        proc = _run(STEPS_FILE, out, ["-D", f"step={step}"],
                    ["--render", "--imgsize=200,150", "--colorscheme=Tomorrow"])
        return step, proc.returncode, _problems(proc), out.exists()


@pytest.mark.slow
def test_every_step_and_the_hero_render_without_warnings():
    with ThreadPoolExecutor(WORKERS) as pool:
        results = list(pool.map(_render, range(1, step_count() + 1)))
    for step, code, problems, made in results:
        assert code == 0 and made, f"step {step} exit {code}"
        assert not problems, f"step {step}: {problems}"
    with tempfile.TemporaryDirectory() as tmp:
        proc = _run(HERO_FILE, Path(tmp) / "hero.png", (), ["--render", "--imgsize=200,150"])
        assert proc.returncode == 0 and not _problems(proc)
