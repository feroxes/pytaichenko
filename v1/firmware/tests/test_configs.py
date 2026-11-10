import json
import os
import re
import subprocess
from pathlib import Path

import pytest
import yaml

FIRMWARE_DIR = Path(__file__).resolve().parents[1]
BASELINE = Path(__file__).with_name("example_baseline.yaml")
WAKE_WORDS = ["hey_jarvis", "okay_nabu", "hey_mycroft"]
MODELS_REPO = "https://github.com/esphome/micro-wake-word-models.git"
MODELS_COMMIT = "40ff33f57f8fc6ad71a75ef085abbf742495b225"
PROJECT_NAME = "Pytaichenko.Speaker"


class ConfigLoader(yaml.SafeLoader):
    pass


def tagged_scalar(loader, tag_suffix, node):
    return {"!" + tag_suffix: loader.construct_scalar(node)}


ConfigLoader.add_multi_constructor("!", tagged_scalar)


def load_config(text):
    return yaml.load(text, Loader=ConfigLoader)


def test_example_config_matches_baseline(esphome_config):
    rendered = esphome_config("example", True)
    if os.environ.get("UPDATE_EXAMPLE_BASELINE") == "1":
        BASELINE.write_text(rendered)
    assert load_config(rendered) == load_config(BASELINE.read_text())


def test_example_takes_wifi_and_keys_from_secrets(esphome_config):
    merged = load_config(esphome_config("example", True))

    assert merged["mdns"]["disabled"] is True
    networks = merged["wifi"]["networks"]
    assert len(networks) == 1
    assert networks[0]["ssid"] == {"!secret": "wifi_ssid"}
    assert networks[0]["password"] == {"!secret": "wifi_password"}

    esphome_ota = [item for item in merged["ota"] if item["platform"] == "esphome"]
    assert len(esphome_ota) == 1
    assert esphome_ota[0]["password"] == {"!secret": "kitchen_ota_password"}

    assert merged["api"]["encryption"]["key"] == {"!secret": "kitchen_api_key"}

    debug_sensors = [item for item in merged["sensor"] if item["platform"] == "debug"]
    assert len(debug_sensors) == 1
    assert debug_sensors[0]["free"]["name"] == "Heap free"
    assert debug_sensors[0]["block"]["name"] == "Heap max block"


@pytest.mark.parametrize(
    "root_name, with_secrets", [("example", True), ("pytaichenko", False)]
)
def test_wake_words(esphome_config, root_name, with_secrets):
    merged = load_config(esphome_config(root_name, with_secrets))
    models = merged["micro_wake_word"]["models"]

    assert [model["id"] for model in models] == WAKE_WORDS
    assert [model["probability_cutoff"] for model in models] == [0.85, 0.80, 0.90]


@pytest.mark.parametrize(
    "root_name, with_secrets", [("example", True), ("pytaichenko", False)]
)
def test_wake_word_models_come_from_one_pinned_upstream_commit(
    esphome_config, root_name, with_secrets
):
    merged = load_config(esphome_config(root_name, with_secrets))
    mww = merged["micro_wake_word"]
    sources = {model["id"]: model["model"] for model in mww["models"]}
    sources["vad"] = mww["vad"]["model"]

    assert sorted(sources) == sorted(WAKE_WORDS + ["vad"])
    for name, source in sources.items():
        assert source["type"] == "git", name
        assert source["url"] == MODELS_REPO, name
        assert source["ref"] == MODELS_COMMIT, name
        assert source["file"] == f"models/v2/{name}.json", name


@pytest.mark.parametrize(
    "root_name, with_secrets", [("example", True), ("pytaichenko", False)]
)
def test_settings_reach_flash_within_seconds(esphome_config, root_name, with_secrets):
    merged = load_config(esphome_config(root_name, with_secrets))

    assert merged["preferences"]["flash_write_interval"] == "5s"


def test_pytaichenko_has_no_secrets(esphome_config):
    sources = [FIRMWARE_DIR / "pytaichenko.yaml"]
    sources += sorted((FIRMWARE_DIR / "packages").glob("*.yaml"))

    for source in sources:
        assert source.exists(), source
        assert "!secret" not in source.read_text(), source

    assert esphome_config("pytaichenko", False)


def test_pytaichenko_settings(esphome_config):
    merged = load_config(esphome_config("pytaichenko", False))

    assert merged["esphome"]["name"] == "pytaichenko"
    assert merged["esphome"]["name_add_mac_suffix"] is True
    assert merged["esphome"]["friendly_name"] == "Питайченко"

    assert "encryption" in merged["api"]
    assert "key" not in (merged["api"]["encryption"] or {})
    assert merged["api"]["reboot_timeout"] == "15min"
    assert merged["provisioning"]["timeout"] == "24h"

    assert "improv_serial" in merged

    for absent in ("ota", "captive_portal", "debug"):
        assert absent not in merged
    assert "ap" not in merged["wifi"]
    assert merged["wifi"]["networks"] == []
    assert (merged.get("mdns") or {}).get("disabled", False) is False


def test_adopted_speaker_restarts_when_the_provisioning_window_closes(esphome_config):
    merged = load_config(esphome_config("pytaichenko", False))
    actions = merged["provisioning"]["on_timeout"]["then"]

    assert len(actions) == 1
    branch = actions[0]["if"]
    condition = branch["condition"]["lambda"]["!lambda"]
    assert condition.strip() == "return api::global_api_server->get_noise_ctx().has_psk();"
    assert [step["lambda"]["!lambda"].strip() for step in branch["then"]] == [
        "App.safe_reboot();"
    ]
    assert "else" not in branch


def test_pytaichenko_names_its_project_with_a_release_version(esphome_config):
    merged = load_config(esphome_config("pytaichenko", False))
    project = merged["esphome"]["project"]

    assert project["name"] == PROJECT_NAME
    assert re.fullmatch(r"\d+\.\d+\.\d+", project["version"])


def test_example_keeps_its_own_identity(esphome_config):
    merged = load_config(esphome_config("example", True))

    assert "project" not in merged["esphome"]
    assert "provisioning" not in merged
    assert merged["api"]["reboot_timeout"] == "15min"


def test_web_manifest_carries_the_project_name_and_version(esphome_config, tmp_path):
    project = load_config(esphome_config("pytaichenko", False))["esphome"]["project"]
    out = tmp_path / "web"
    result = subprocess.run(
        ["bash", str(FIRMWARE_DIR / "build-web.sh"), "--manifest-only"],
        env={"PATH": "/usr/bin:/bin", "WEB_OUT_DIR": str(out)},
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr

    manifest = json.loads((out / "manifest.json").read_text())
    assert manifest["name"] == project["name"]
    assert manifest["version"] == project["version"]


@pytest.mark.parametrize(
    "name_line, version_line",
    [
        ("name: Pytaichenko.Speaker", 'version: "1.2.3"'),
        ("name: 'Pytaichenko.Speaker'", "version: '1.2.3'"),
        ("name: Pytaichenko.Speaker  # shown by the installer", "version: 1.2.3 # bump on every change"),
        ('name: "Pytaichenko.Speaker" # quoted', "version: '1.2.3'   # quoted too"),
    ],
)
def test_web_manifest_reads_quoted_and_commented_project_fields(tmp_path, name_line, version_line):
    firmware = tmp_path / "firmware"
    firmware.mkdir()
    (firmware / "build-web.sh").write_text((FIRMWARE_DIR / "build-web.sh").read_text())
    (firmware / "pytaichenko.yaml").write_text(
        "esphome:\n"
        "  name_add_mac_suffix: true\n"
        "  project:\n"
        f"    {name_line}\n"
        f"    {version_line}\n"
        "wifi:\n"
    )
    out = tmp_path / "web"
    result = subprocess.run(
        ["bash", str(firmware / "build-web.sh"), "--manifest-only"],
        env={"PATH": "/usr/bin:/bin", "WEB_OUT_DIR": str(out)},
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr

    manifest = json.loads((out / "manifest.json").read_text())
    assert manifest["name"] == PROJECT_NAME
    assert manifest["version"] == "1.2.3"


def script_actions(name):
    package = load_config((FIRMWARE_DIR / "packages" / "assistant.yaml").read_text())
    scripts = [script for script in package["script"] if script["id"] == name]
    assert len(scripts) == 1, name
    return scripts[0]["then"]


def ifs_running(actions, action):
    found = []
    for item in actions:
        branch = item.get("if") if isinstance(item, dict) else None
        if branch is None:
            continue
        if any(action in step for step in branch.get("then", [])):
            found.append(branch)
        for key in ("then", "else"):
            found += ifs_running(branch.get(key, []), action)
    return found


def checks_connection(condition):
    if "voice_assistant.connected" in condition:
        return True
    return any("voice_assistant.connected" in part for part in condition.get("and", []))


def guards(actions, action, guarded=False):
    found = []
    for item in actions:
        if not isinstance(item, dict):
            continue
        if action in item:
            found.append(guarded)
        branch = item.get("if")
        if branch is None:
            continue
        inside = guarded or checks_connection(branch["condition"])
        found += guards(branch.get("then", []), action, inside)
        found += guards(branch.get("else", []), action, guarded)
    return found


def test_the_guard_walk_finds_every_start_and_whether_a_connection_check_covers_it():
    connected = {"voice_assistant.connected": None}
    start = {"micro_wake_word.start": None}
    actions = [
        start,
        {"if": {"condition": connected, "then": [start], "else": [start]}},
        {"if": {"condition": {"lambda": "x"}, "then": [start]}},
        {"if": {"condition": {"and": [connected]}, "then": [{"if": {"condition": {"lambda": "x"}, "then": [start]}}]}},
    ]

    assert guards(actions, "micro_wake_word.start") == [False, True, False, False, True]


def test_wake_word_restarts_only_while_the_server_is_connected():
    actions = script_actions("back_to_wake_word")
    starts = guards(actions, "micro_wake_word.start")

    assert starts
    assert all(starts), starts


def test_listening_starts_only_while_connected_and_falls_back_to_offline():
    actions = script_actions("start_listening")

    assert guards(actions, "voice_assistant.start") == [True]
    starts = ifs_running(actions, "voice_assistant.start")
    assert len(starts) == 1
    branch = starts[0]
    assert checks_connection(branch["condition"])
    offline = [step["light.turn_on"] for step in branch["else"] if "light.turn_on" in step]
    assert {(light["id"], light["effect"]) for light in offline} == {
        ("led", "Offline"),
        ("ring", "Offline"),
    }
