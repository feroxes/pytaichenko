import base64
import json
import os
import re
import stat
from dataclasses import replace
from pathlib import Path

import pytest

from voice_hub import settings as settings_module
from voice_hub.config import Config, ConfigError, OpenAIConfig, Timing
from voice_hub.prompts import default_instructions
from voice_hub.settings import (
    TRANSCRIPTION_MODEL,
    AssistantSettings,
    DeviceSettings,
    OpenAISettings,
    Settings,
    SettingsError,
    SettingsStore,
    apply_patch,
    check_address,
    check_server_port,
    device_config,
    openai_config,
    public_view,
    seed,
)

OPENAI_MARKER = "sk-marker-openai-0123456789"
HASH_MARKER = "scrypt$marker-hash-0123456789"


def device_key(n: int) -> str:
    return base64.b64encode(bytes([n]) * 32).decode("ascii")


KEY_A = device_key(1)
KEY_B = device_key(2)

YAML = (
    "http_port: 8081\n"
    "advertise_host: 192.0.2.60\n"
    "devices:\n"
    "  - name: kitchen\n"
    "    host: 192.0.2.50\n"
    "    encryption_key_env: KITCHEN_API_KEY\n"
    "  - name: hall\n"
    "    host: 192.0.2.51\n"
    "    port: 6054\n"
    "    encryption_key_env: HALL_API_KEY\n"
    "    debug_input_wav: /tmp/in.wav\n"
    "openai:\n"
    "  model: gpt-realtime-2.1\n"
    "  voice: cedar\n"
    "  transcription_model: gpt-4o-mini-transcribe\n"
    "  reasoning_effort: minimal\n"
    "  vad_threshold: 0.7\n"
    "  silence_ms: 500\n"
    "timing:\n"
    "  follow_up_s: 5\n"
    "  max_follow_ups: 3\n"
)

ENV = {"OPENAI_API_KEY": OPENAI_MARKER, "KITCHEN_API_KEY": KEY_A, "HALL_API_KEY": KEY_B}


def write(tmp_path: Path, text: str, name: str = "config.yaml") -> Path:
    path = tmp_path / name
    path.write_text(text, encoding="utf-8")
    return path


def full_settings() -> Settings:
    return Settings(
        openai=OpenAISettings(
            api_key=OPENAI_MARKER, model="gpt-realtime-2.1", reasoning_effort="minimal", log_heard=True
        ),
        assistant=AssistantSettings(language="en", instructions="Be brief."),
        timing=Timing(follow_up_s=6.0, max_follow_ups=4),
        devices=(
            DeviceSettings(name="kitchen", host="192.0.2.50", key=KEY_A),
            DeviceSettings(name="hall", host="192.0.2.51", port=6054, key=None),
        ),
        password_hash=HASH_MARKER,
        http_port=8090,
        advertise_host="192.0.2.60",
    )


@pytest.fixture
def store(tmp_path) -> SettingsStore:
    return SettingsStore(tmp_path / "data" / "settings.json")


def test_seed_from_yaml_and_env(tmp_path):
    s = seed(write(tmp_path, YAML), ENV)
    assert s.openai == OpenAISettings(
        api_key=OPENAI_MARKER,
        model="gpt-realtime-2.1",
        voice="cedar",
        reasoning_effort="minimal",
        vad_threshold=0.7,
        silence_ms=500,
        log_heard=True,
    )
    assert s.assistant == AssistantSettings(language="uk", instructions=default_instructions("uk"))
    assert s.timing == Timing(follow_up_s=5.0, max_follow_ups=3)
    assert s.devices == (
        DeviceSettings(name="kitchen", host="192.0.2.50", port=6053, key=KEY_A),
        DeviceSettings(name="hall", host="192.0.2.51", port=6054, key=KEY_B, debug_input_wav="/tmp/in.wav"),
    )
    assert s.password_hash is None
    assert s.http_port == 8081 and s.advertise_host == "192.0.2.60"


def test_seed_without_transcription_model_does_not_log_what_was_heard(tmp_path):
    text = "devices:\n  - name: kitchen\n    host: 192.0.2.50\n    encryption_key_env: KITCHEN_API_KEY\n"
    s = seed(write(tmp_path, text), ENV)
    assert s.openai.log_heard is False
    assert s.openai.model == "gpt-realtime-2.1-mini" and s.openai.reasoning_effort == "low"
    assert s.http_port == 8080 and s.advertise_host is None and s.timing == Timing()


def test_seed_without_config_uses_defaults_and_the_openai_key(tmp_path):
    assert seed(None, {"OPENAI_API_KEY": OPENAI_MARKER}) == Settings(openai=OpenAISettings(api_key=OPENAI_MARKER))


def test_seed_without_anything_has_no_key_and_no_devices():
    s = seed(None, {})
    assert s == Settings()
    assert s.openai.api_key is None and s.devices == () and s.password_hash is None
    assert s.assistant.language == "uk" and s.assistant.instructions == default_instructions("uk")


def test_seed_treats_an_empty_openai_key_as_missing(tmp_path):
    assert seed(None, {"OPENAI_API_KEY": ""}).openai.api_key is None
    text = "devices: []\n"
    assert seed(write(tmp_path, text), {"OPENAI_API_KEY": ""}).openai.api_key is None


def test_seed_needs_the_device_key_named_in_the_config(tmp_path):
    env = {"OPENAI_API_KEY": OPENAI_MARKER, "KITCHEN_API_KEY": KEY_A}
    with pytest.raises(ConfigError, match="HALL_API_KEY"):
        seed(write(tmp_path, YAML), env)


def test_seed_rejects_a_malformed_device_key_without_showing_it(tmp_path):
    env = dict(ENV, HALL_API_KEY="not-a-noise-key-marker")
    with pytest.raises(SettingsError) as info:
        seed(write(tmp_path, YAML), env)
    assert "devices[1].key" in str(info.value)
    assert "not-a-noise-key-marker" not in str(info.value)


def test_seed_reports_a_bad_config(tmp_path):
    with pytest.raises(ConfigError, match="modle"):
        seed(write(tmp_path, "devices: []\nopenai:\n  modle: x\n"), ENV)
    with pytest.raises(ConfigError, match="missing.yaml"):
        seed(tmp_path / "missing.yaml", ENV)


def test_save_and_load_round_trip(store):
    s = full_settings()
    store.save(s)
    assert store.load() == s


def test_round_trip_keeps_debug_input(store):
    s = Settings(devices=(DeviceSettings(name="kitchen", host="192.0.2.50", key=KEY_A, debug_input_wav="/tmp/q.wav"),))
    store.save(s)
    assert store.load() == s


def test_load_missing_file_returns_none(store):
    assert store.load() is None


def test_saved_file_is_readable_json(store):
    store.save(full_settings())
    data = json.loads(store.path.read_text(encoding="utf-8"))
    assert data["version"] == 1
    assert data["assistant"] == {"language": "en", "instructions": "Be brief."}
    assert data["devices"][0]["name"] == "kitchen"


def test_saved_file_is_private(store):
    store.save(full_settings())
    assert stat.S_IMODE(store.path.stat().st_mode) == 0o600


def test_saving_over_an_open_file_makes_it_private(store):
    store.path.parent.mkdir(parents=True)
    store.path.write_text("{}")
    os.chmod(store.path, 0o644)
    store.save(Settings())
    assert stat.S_IMODE(store.path.stat().st_mode) == 0o600


def test_save_creates_the_data_directory(store):
    assert not store.path.parent.exists()
    store.save(Settings())
    assert store.path.exists()


def leftovers(store: SettingsStore) -> list[str]:
    return sorted(p.name for p in store.path.parent.iterdir() if p.name != store.path.name)


@pytest.mark.parametrize("broken", ["fsync", "replace"])
def test_failed_save_keeps_the_old_file(store, monkeypatch, broken):
    old = full_settings()
    store.save(old)
    before = store.path.read_bytes()

    def fail(*args, **kwargs):
        raise OSError(5, "disk on fire")

    monkeypatch.setattr(settings_module.os, broken, fail)
    with pytest.raises(SettingsError, match="disk on fire"):
        store.save(Settings())
    monkeypatch.undo()
    assert store.path.read_bytes() == before
    assert store.load() == old
    assert leftovers(store) == []


def test_failed_write_keeps_the_old_file(store, monkeypatch):
    old = full_settings()
    store.save(old)
    real_fdopen = os.fdopen

    class HalfWriter:
        def __init__(self, f):
            self._f = f

        def __enter__(self):
            return self

        def __exit__(self, *exc):
            self._f.close()
            return False

        def write(self, text):
            self._f.write(text[: len(text) // 2])
            raise OSError(28, "no space left")

    monkeypatch.setattr(settings_module.os, "fdopen", lambda fd, *a, **kw: HalfWriter(real_fdopen(fd, *a, **kw)))
    with pytest.raises(SettingsError, match="no space left"):
        store.save(Settings())
    monkeypatch.undo()
    assert store.load() == old
    assert leftovers(store) == []


def test_save_refuses_invalid_settings_and_leaves_the_file(store):
    store.save(Settings())
    before = store.path.read_bytes()
    twins = (DeviceSettings(name="kitchen", host="192.0.2.50"), DeviceSettings(name="kitchen", host="192.0.2.51"))
    with pytest.raises(SettingsError, match="kitchen"):
        store.save(Settings(devices=twins))
    assert store.path.read_bytes() == before


@pytest.mark.parametrize(
    "text",
    [
        "",
        "{",
        '{"version": 1, "openai": {"model": "gpt',
        "not json at all",
        "[]",
        "null",
        '"settings"',
    ],
)
def test_damaged_file_raises(store, text):
    store.path.parent.mkdir(parents=True)
    store.path.write_text(text, encoding="utf-8")
    with pytest.raises(SettingsError, match="settings.json"):
        store.load()


def test_half_written_file_raises(store):
    store.save(full_settings())
    data = store.path.read_bytes()
    store.path.write_bytes(data[: len(data) // 2])
    with pytest.raises(SettingsError):
        store.load()


def test_binary_garbage_raises(store):
    store.path.parent.mkdir(parents=True)
    store.path.write_bytes(b"\xff\xfe\x00garbage")
    with pytest.raises(SettingsError):
        store.load()


def saved_data(store: SettingsStore) -> dict:
    store.save(full_settings())
    return json.loads(store.path.read_text(encoding="utf-8"))


@pytest.mark.parametrize(
    ("where", "change"),
    [
        ("version", lambda d: d.update(version=2)),
        ("colour", lambda d: d.update(colour="red")),
        ("openai.modle", lambda d: d["openai"].update(modle="x")),
        ("openai.vad_threshold", lambda d: d["openai"].update(vad_threshold=2)),
        ("openai.log_heard", lambda d: d["openai"].update(log_heard="yes")),
        ("assistant.language", lambda d: d["assistant"].update(language="de")),
        ("assistant.instructions", lambda d: d["assistant"].update(instructions=None)),
        ("timing.follow_up_s", lambda d: d["timing"].update(follow_up_s=-1)),
        ("devices", lambda d: d.update(devices={})),
        ("devices[0].host", lambda d: d["devices"][0].pop("host")),
        ("devices[1]", lambda d: d["devices"][1].update(name="kitchen")),
        ("password_hash", lambda d: d.update(password_hash="")),
        ("http_port", lambda d: d.update(http_port=0)),
    ],
)
def test_invalid_file_content_raises_without_secrets(store, where, change):
    data = saved_data(store)
    change(data)
    store.path.write_text(json.dumps(data), encoding="utf-8")
    with pytest.raises(SettingsError) as info:
        store.load()
    message = str(info.value)
    assert where in message
    for secret in (OPENAI_MARKER, HASH_MARKER, KEY_A):
        assert secret not in message


def test_file_may_leave_out_fields_that_have_defaults(store):
    store.path.parent.mkdir(parents=True)
    store.path.write_text('{"version": 1, "openai": {"model": "gpt-realtime-2.1"}}', encoding="utf-8")
    assert store.load() == Settings(openai=OpenAISettings(model="gpt-realtime-2.1"))


def test_unreadable_path_raises(tmp_path):
    folder = tmp_path / "settings.json"
    folder.mkdir()
    with pytest.raises(SettingsError, match="settings.json"):
        SettingsStore(folder).load()


def test_repr_hides_secrets():
    text = repr(full_settings())
    for secret in (OPENAI_MARKER, HASH_MARKER, KEY_A):
        assert secret not in text
    assert "kitchen" in text and "gpt-realtime-2.1" in text


def test_patch_merges_sections():
    s = apply_patch(
        full_settings(),
        {
            "openai": {"model": "gpt-realtime-2.1-mini", "vad_threshold": 1, "log_heard": False},
            "timing": {"first_turn_s": 4, "max_follow_ups": 0},
            "http_port": 8095,
            "advertise_host": None,
        },
    )
    assert s.openai.model == "gpt-realtime-2.1-mini" and s.openai.voice == "marin"
    assert s.openai.vad_threshold == 1.0 and isinstance(s.openai.vad_threshold, float)
    assert s.openai.log_heard is False and s.openai.api_key == OPENAI_MARKER
    assert s.timing.first_turn_s == 4.0 and s.timing.follow_up_s == 6.0 and s.timing.max_follow_ups == 0
    assert s.http_port == 8095 and s.advertise_host is None
    assert s.password_hash == HASH_MARKER and s.devices == full_settings().devices


def test_empty_patch_changes_nothing():
    assert apply_patch(full_settings(), {}) == full_settings()


def test_patch_trims_text_fields():
    s = apply_patch(Settings(), {"openai": {"model": " gpt-realtime-2.1 ", "api_key": f" {OPENAI_MARKER}\n"}})
    assert s.openai.model == "gpt-realtime-2.1" and s.openai.api_key == OPENAI_MARKER


def test_patch_sets_and_clears_the_openai_key():
    s = apply_patch(Settings(), {"openai": {"api_key": OPENAI_MARKER}})
    assert s.openai.api_key == OPENAI_MARKER
    assert apply_patch(s, {"openai": {"api_key": None}}).openai.api_key is None


def test_switching_language_replaces_default_instructions():
    s = apply_patch(Settings(), {"assistant": {"language": "en"}})
    assert s.assistant == AssistantSettings(language="en", instructions=default_instructions("en"))
    back = apply_patch(s, {"assistant": {"language": "uk"}})
    assert back.assistant == AssistantSettings()


def test_switching_language_keeps_edited_instructions():
    edited = Settings(assistant=AssistantSettings(instructions="Ти — Марта."))
    s = apply_patch(edited, {"assistant": {"language": "en"}})
    assert s.assistant == AssistantSettings(language="en", instructions="Ти — Марта.")


def test_patch_sets_language_and_instructions_together():
    s = apply_patch(Settings(), {"assistant": {"language": "en", "instructions": "Be brief."}})
    assert s.assistant == AssistantSettings(language="en", instructions="Be brief.")


def test_null_instructions_restore_the_defaults_of_the_language():
    edited = Settings(assistant=AssistantSettings(language="en", instructions="Be brief."))
    restored = apply_patch(edited, {"assistant": {"instructions": None}})
    assert restored.assistant.instructions == default_instructions("en")
    s = apply_patch(edited, {"assistant": {"language": "uk", "instructions": None}})
    assert s.assistant == AssistantSettings()


def test_patch_replaces_devices_and_keeps_keys_by_name():
    s = apply_patch(
        full_settings(),
        {"devices": [{"name": "kitchen", "host": "192.0.2.70"}, {"name": "porch", "host": "192.0.2.71", "key": KEY_B}]},
    )
    assert s.devices == (
        DeviceSettings(name="kitchen", host="192.0.2.70", port=6053, key=KEY_A),
        DeviceSettings(name="porch", host="192.0.2.71", port=6053, key=KEY_B),
    )


def test_patch_keeps_the_port_and_debug_input_of_a_known_device():
    hall = DeviceSettings(name="hall", host="192.0.2.51", port=6054, key=KEY_A, debug_input_wav="/q.wav")
    s = Settings(devices=(hall,))
    patched = apply_patch(s, {"devices": [{"name": "hall", "host": "192.0.2.52"}]})
    assert patched.devices == (
        DeviceSettings(name="hall", host="192.0.2.52", port=6054, key=KEY_A, debug_input_wav="/q.wav"),
    )


def test_patch_can_drop_a_device_key_and_all_devices():
    s = apply_patch(full_settings(), {"devices": [{"name": "kitchen", "host": "192.0.2.50", "key": None}]})
    assert s.devices == (DeviceSettings(name="kitchen", host="192.0.2.50"),)
    assert apply_patch(s, {"devices": []}).devices == ()


def test_new_device_without_a_key_waits_to_be_adopted():
    s = apply_patch(Settings(), {"devices": [{"name": "porch", "host": "192.0.2.71", "port": 6055}]})
    assert s.devices == (DeviceSettings(name="porch", host="192.0.2.71", port=6055, key=None),)


@pytest.mark.parametrize(
    ("patch", "where"),
    [
        ([], "patch"),
        ({"colour": "red"}, "colour"),
        ({"password_hash": "x"}, "password_hash"),
        ({"version": 2}, "version"),
        ({"openai": []}, "openai"),
        ({"openai": {"modle": "x"}}, "openai.modle"),
        ({"openai": {"model": ""}}, "openai.model"),
        ({"openai": {"model": None}}, "openai.model"),
        ({"openai": {"voice": "  "}}, "openai.voice"),
        ({"openai": {"voice": 5}}, "openai.voice"),
        ({"openai": {"api_key": ""}}, "openai.api_key"),
        ({"openai": {"reasoning_effort": ""}}, "openai.reasoning_effort"),
        ({"openai": {"reasoning_effort": 3}}, "openai.reasoning_effort"),
        ({"openai": {"vad_threshold": 1.5}}, "openai.vad_threshold"),
        ({"openai": {"vad_threshold": -0.1}}, "openai.vad_threshold"),
        ({"openai": {"vad_threshold": True}}, "openai.vad_threshold"),
        ({"openai": {"vad_threshold": "0.5"}}, "openai.vad_threshold"),
        ({"openai": {"vad_threshold": float("nan")}}, "openai.vad_threshold"),
        ({"openai": {"silence_ms": 0}}, "openai.silence_ms"),
        ({"openai": {"silence_ms": 400.5}}, "openai.silence_ms"),
        ({"openai": {"silence_ms": True}}, "openai.silence_ms"),
        ({"openai": {"log_heard": 1}}, "openai.log_heard"),
        ({"assistant": {"language": "de"}}, "assistant.language"),
        ({"assistant": {"language": None}}, "assistant.language"),
        ({"assistant": {"instructions": ""}}, "assistant.instructions"),
        ({"assistant": {"instructions": "   "}}, "assistant.instructions"),
        ({"assistant": {"instructions": 5}}, "assistant.instructions"),
        ({"assistant": {"name": "Марта"}}, "assistant.name"),
        ({"timing": {"follow_up_s": 0}}, "timing.follow_up_s"),
        ({"timing": {"first_turn_s": -1}}, "timing.first_turn_s"),
        ({"timing": {"mic_holdoff_s": float("inf")}}, "timing.mic_holdoff_s"),
        ({"timing": {"session_idle_s": "300"}}, "timing.session_idle_s"),
        ({"timing": {"answer_max_s": False}}, "timing.answer_max_s"),
        ({"timing": {"max_follow_ups": -1}}, "timing.max_follow_ups"),
        ({"timing": {"max_follow_ups": 2.5}}, "timing.max_follow_ups"),
        ({"timing": {"folow_up_s": 5}}, "timing.folow_up_s"),
        ({"devices": {}}, "devices"),
        ({"devices": ["kitchen"]}, "devices[0]"),
        ({"devices": [{"name": "porch"}]}, "devices[0].host"),
        ({"devices": [{"host": "192.0.2.71"}]}, "devices[0].name"),
        ({"devices": [{"name": "", "host": "192.0.2.71"}]}, "devices[0].name"),
        ({"devices": [{"name": "porch", "host": "192.0.2.71", "port": 0}]}, "devices[0].port"),
        ({"devices": [{"name": "porch", "host": "192.0.2.71", "port": 65536}]}, "devices[0].port"),
        ({"devices": [{"name": "porch", "host": "192.0.2.71", "port": "6053"}]}, "devices[0].port"),
        ({"devices": [{"name": "porch", "host": "192.0.2.71", "key": "short"}]}, "devices[0].key"),
        ({"devices": [{"name": "porch", "host": "192.0.2.71", "key": ""}]}, "devices[0].key"),
        (
            {"devices": [{"name": "porch", "host": "192.0.2.71", "debug_input_wav": "/x.wav"}]},
            "devices[0].debug_input_wav",
        ),
        ({"devices": [{"name": "a", "host": "192.0.2.71"}, {"name": "a", "host": "192.0.2.72"}]}, "devices[1]"),
        ({"devices": [{"name": "a", "host": "192.0.2.71"}, {"name": " a ", "host": "192.0.2.72"}]}, "devices[1]"),
        ({"http_port": 0}, "http_port"),
        ({"http_port": 65536}, "http_port"),
        ({"http_port": "8080"}, "http_port"),
        ({"advertise_host": ""}, "advertise_host"),
        ({"advertise_host": 5}, "advertise_host"),
    ],
)
def test_patch_rejects_bad_values(patch, where):
    with pytest.raises(SettingsError) as info:
        apply_patch(full_settings(), patch)
    message = str(info.value)
    assert where in message
    for secret in (OPENAI_MARKER, HASH_MARKER, KEY_A, KEY_B):
        assert secret not in message


def test_bad_key_in_patch_is_not_echoed():
    with pytest.raises(SettingsError) as info:
        apply_patch(Settings(), {"devices": [{"name": "porch", "host": "192.0.2.71", "key": "secret-but-bad"}]})
    assert "secret-but-bad" not in str(info.value)


def test_patch_accepts_a_valid_key_written_with_spaces():
    s = apply_patch(Settings(), {"devices": [{"name": "porch", "host": "192.0.2.71", "key": f" {KEY_B} "}]})
    assert s.devices[0].key == KEY_B


def test_public_view_has_no_secrets():
    view = public_view(full_settings())
    text = json.dumps(view, ensure_ascii=False)
    for secret in (OPENAI_MARKER, HASH_MARKER, KEY_A):
        assert secret not in text
    assert "api_key" not in view["openai"] and "password_hash" not in view
    assert all("key" not in device for device in view["devices"])


def test_public_view_shows_what_is_set():
    view = public_view(full_settings())
    assert view["openai"] == {
        "api_key_set": True,
        "model": "gpt-realtime-2.1",
        "voice": "marin",
        "reasoning_effort": "minimal",
        "vad_threshold": 0.5,
        "silence_ms": 600,
        "log_heard": True,
    }
    assert view["assistant"] == {"language": "en", "instructions": "Be brief."}
    assert view["timing"]["follow_up_s"] == 6.0 and view["timing"]["max_follow_ups"] == 4
    assert view["devices"] == [
        {"name": "kitchen", "host": "192.0.2.50", "port": 6053, "key_set": True},
        {"name": "hall", "host": "192.0.2.51", "port": 6054, "key_set": False},
    ]
    assert view["password_set"] is True
    assert view["http_port"] == 8090 and view["advertise_host"] == "192.0.2.60"


def test_public_view_of_fresh_settings():
    view = public_view(Settings())
    assert view["openai"]["api_key_set"] is False and view["password_set"] is False and view["devices"] == []
    json.dumps(view)


def test_openai_config_follows_the_settings():
    assert openai_config(full_settings()) == OpenAIConfig(
        api_key=OPENAI_MARKER,
        model="gpt-realtime-2.1",
        voice="marin",
        instructions="Be brief.",
        reasoning_effort="minimal",
        vad_threshold=0.5,
        silence_ms=600,
        transcription_model=TRANSCRIPTION_MODEL,
        language="en",
    )


def test_device_config_follows_the_settings():
    s = full_settings()
    cfg = device_config(s.devices[0])
    assert (cfg.name, cfg.host, cfg.port, cfg.encryption_key, cfg.debug_input_wav) == (
        "kitchen", "192.0.2.50", 6053, KEY_A, None
    )
    assert KEY_A not in repr(cfg)


def test_device_config_passes_debug_input():
    d = DeviceSettings(name="kitchen", host="192.0.2.50", key=KEY_A, debug_input_wav="/tmp/q.wav")
    assert device_config(d).debug_input_wav == Path("/tmp/q.wav")


def test_device_config_needs_a_key():
    with pytest.raises(ValueError, match="kitchen"):
        device_config(DeviceSettings(name="kitchen", host="192.0.2.50"))


def test_openai_config_without_log_heard_has_no_transcription():
    assert openai_config(Settings()).transcription_model is None


def test_default_settings_match_the_runtime_defaults():
    assert openai_config(Settings()) == OpenAIConfig(api_key=None)
    assert Settings().timing == Timing() and Settings().http_port == Config.__dataclass_fields__["http_port"].default
    assert Settings().advertise_host is None


@pytest.mark.parametrize("host, port", [("192.0.2.30", 6053), (" 192.0.2.30 ", 1), ("speaker.local", 65535)])
def test_check_address_accepts_good_addresses(host, port):
    assert check_address(host, port) == (host.strip(), port)


@pytest.mark.parametrize(
    "host, port, field",
    [("", 6053, "host"), ("  ", 6053, "host"), (None, 6053, "host"), (5, 6053, "host"), ("192.0.2.30", 0, "port"),
     ("192.0.2.30", 65536, "port"), ("192.0.2.30", "6053", "port"), ("192.0.2.30", True, "port"),
     ("\ud800", 6053, "host")],
)
def test_check_address_rejects_bad_addresses(host, port, field):
    with pytest.raises(SettingsError, match=f"^{field}:"):
        check_address(host, port)


HOST_RULE = "must be a host name or an IP address, without http://, a port or a path"
GOOD_HOSTS = ["192.0.2.30", "speaker.local", "pytaichenko-1a2b3c", "hub.home.arpa.", "2001:db8::5", "my_speaker.lan",
              "123.lan", "4u.home"]
ZONED_HOSTS = ["fe80::1%eth0", "fe80::1%en0.1", "fe80::1%wlan_0-1", "fe80::1%7"]
BAD_HOSTS = ["http://192.0.2.30", "192.0.2.30:6053", "192.0.2.30/", "hub.lan/audio", "my speaker.local", "[2001:db8::5]",
             "user@hub.lan", "x..y", "hub.lan?x", "a" * 64 + ".lan", "192.168.1", "1.2.3", "192.0.2.300",
             "192.168.010.20", "192.0.2.30.", "12345", "hub.123", "fe80::1%a b", "fe80::1%eth0 x", "fe80::1%",
             "fe80::1%eth/0", "fe80::1%eth0:1", "192.0.2.30%eth0"]


@pytest.mark.parametrize("host", GOOD_HOSTS)
def test_hosts_are_names_or_ip_addresses(host, store):
    assert check_address(host, 6053) == (host, 6053)
    s = apply_patch(Settings(), {"advertise_host": host, "devices": [{"name": "porch", "host": host}]})
    assert s.advertise_host == host and s.devices[0].host == host
    store.save(s)
    assert store.load() == s


@pytest.mark.parametrize("host", ZONED_HOSTS)
def test_an_ipv6_zone_suits_a_speaker_but_not_the_server_address(host, store):
    assert check_address(host, 6053) == (host, 6053)
    s = apply_patch(Settings(), {"devices": [{"name": "porch", "host": host}]})
    assert s.devices[0].host == host
    store.save(s)
    assert store.load() == s
    with pytest.raises(SettingsError, match=rf"^advertise_host: {HOST_RULE}$"):
        apply_patch(Settings(), {"advertise_host": host})
    store.path.write_text(json.dumps({"version": 1, "advertise_host": host}), encoding="utf-8")
    with pytest.raises(SettingsError, match="advertise_host"):
        store.load()


@pytest.mark.parametrize("host", BAD_HOSTS)
def test_urls_ports_paths_and_spaces_are_not_hosts(host, store):
    with pytest.raises(SettingsError, match=rf"^host: {HOST_RULE}$"):
        check_address(host, 6053)
    with pytest.raises(SettingsError, match=rf"^advertise_host: {HOST_RULE}$"):
        apply_patch(Settings(), {"advertise_host": host})
    with pytest.raises(SettingsError, match=rf"^devices\[0\]\.host: {HOST_RULE}$"):
        apply_patch(Settings(), {"devices": [{"name": "porch", "host": host}]})
    store.path.parent.mkdir(parents=True, exist_ok=True)
    store.path.write_text(json.dumps({"version": 1, "advertise_host": host}), encoding="utf-8")
    with pytest.raises(SettingsError, match="advertise_host"):
        store.load()


def test_seed_refuses_a_url_as_a_host(tmp_path):
    with pytest.raises(SettingsError, match="advertise_host"):
        seed(write(tmp_path, "devices: []\nadvertise_host: http://192.0.2.60:8080\n"), ENV)
    with pytest.raises(SettingsError, match=r"devices\[0\]\.host"):
        seed(write(tmp_path, "devices:\n  - name: kitchen\n    host: 192.0.2.50:6053\n"
                             "    encryption_key_env: KITCHEN_API_KEY\n"), ENV)


@pytest.mark.parametrize("port", [1024, 8080, 65535])
def test_the_server_port_may_be_1024_to_65535(port, store):
    s = apply_patch(Settings(), {"http_port": port})
    assert s.http_port == port and check_server_port(port) == port
    store.save(s)
    assert store.load() == s


@pytest.mark.parametrize("port", [0, 80, 443, 1023, 65536])
def test_the_server_port_below_1024_is_refused_in_the_page_the_file_and_the_yaml(port, tmp_path, store):
    rule = "http_port: must be a whole number from 1024 to 65535"
    with pytest.raises(SettingsError, match=f"^{rule}$"):
        apply_patch(Settings(), {"http_port": port})
    with pytest.raises(SettingsError, match=f"^{rule}$"):
        check_server_port(port)
    store.path.parent.mkdir(parents=True, exist_ok=True)
    store.path.write_text(json.dumps({"version": 1, "http_port": port}), encoding="utf-8")
    with pytest.raises(SettingsError, match=rule):
        store.load()
    with pytest.raises(SettingsError, match=rule):
        seed(write(tmp_path, f"devices: []\nhttp_port: {port}\n"), ENV)


@pytest.mark.parametrize("port", [80, "8080", None, 70000])
def test_a_bad_port_in_the_file_says_how_to_start_again(port, store):
    store.path.parent.mkdir(parents=True, exist_ok=True)
    store.path.write_text(json.dumps({"version": 1, "http_port": port}), encoding="utf-8")
    with pytest.raises(SettingsError) as info:
        store.load()
    message = str(info.value)
    assert message.startswith(f"{store.path}: http_port: must be a whole number from 1024 to 65535; ")
    assert "VOICE_HUB_PORT" in message and ".env" in message and "data/settings.json" in message
    assert "\n" not in message


def test_the_port_variable_stands_in_for_a_bad_port_in_the_file(store, caplog):
    store.path.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps({"version": 1, "http_port": 80, "advertise_host": "hub.lan"})
    store.path.write_text(text, encoding="utf-8")
    loaded = store.load(9099)
    assert loaded == Settings(http_port=9099, advertise_host="hub.lan")
    assert "http_port" in caplog.text and "VOICE_HUB_PORT" in caplog.text
    assert store.path.read_text(encoding="utf-8") == text
    store.save(loaded)
    assert store.load().http_port == 9099


def test_the_port_variable_leaves_a_good_port_in_the_file_and_other_errors_alone(store):
    store.save(Settings(http_port=8081))
    assert store.load(9099).http_port == 8081
    store.path.write_text(json.dumps({"version": 1, "http_port": 80, "advertise_host": "http://hub"}), encoding="utf-8")
    with pytest.raises(SettingsError, match=rf"^{re.escape(str(store.path))}: advertise_host: "):
        store.load(9099)


def test_seed_names_the_config_file_when_it_refuses_a_value(tmp_path):
    path = write(tmp_path, "devices: []\nhttp_port: 80\n")
    with pytest.raises(SettingsError) as info:
        seed(path, ENV)
    assert str(info.value) == f"{path}: http_port: must be a whole number from 1024 to 65535"
    path = write(tmp_path, "devices:\n  - name: kitchen\n    host: 192.0.2.50:6053\n"
                           "    encryption_key_env: KITCHEN_API_KEY\n")
    with pytest.raises(SettingsError, match=rf"^{re.escape(str(path))}: devices\[0\]\.host: "):
        seed(path, ENV)


def test_check_server_port_names_where_the_port_came_from():
    with pytest.raises(SettingsError, match="^VOICE_HUB_PORT: must be a whole number from 1024 to 65535$"):
        check_server_port("80", "VOICE_HUB_PORT")


def test_speaker_ports_stay_1_to_65535():
    assert check_address("192.0.2.30", 80) == ("192.0.2.30", 80)
    assert apply_patch(Settings(), {"devices": [{"name": "porch", "host": "192.0.2.30", "port": 1}]}).devices[0].port == 1


def test_seeded_settings_survive_a_round_trip(tmp_path, store):
    s = seed(write(tmp_path, YAML), ENV)
    store.save(s)
    assert store.load() == s


def test_settings_are_frozen():
    s = Settings()
    with pytest.raises(AttributeError):
        s.http_port = 1
    assert replace(s, http_port=9000).http_port == 9000


ZERO_TIMINGS = ("mic_holdoff_s",)
POSITIVE_TIMINGS = tuple(
    name for name in Timing.__dataclass_fields__
    if name not in ZERO_TIMINGS and name not in ("max_follow_ups", "mic_stop_grace_s")
)


@pytest.mark.parametrize("name", ZERO_TIMINGS)
def test_mic_hold_off_may_be_zero(name, store):
    s = apply_patch(Settings(), {"timing": {name: 0}})
    assert getattr(s.timing, name) == 0.0
    store.save(s)
    assert store.load() == s


@pytest.mark.parametrize("name", ZERO_TIMINGS)
def test_zero_hold_off_seeds_from_yaml(tmp_path, name):
    s = seed(write(tmp_path, f"devices: []\ntiming:\n  {name}: 0\n"), ENV)
    assert getattr(s.timing, name) == 0.0


@pytest.mark.parametrize("name", ZERO_TIMINGS)
def test_negative_hold_off_is_rejected(name):
    with pytest.raises(SettingsError, match=f"timing.{name}: must be 0 or more seconds"):
        apply_patch(Settings(), {"timing": {name: -0.1}})


@pytest.mark.parametrize("value", [0, 0.49, -1])
def test_stop_grace_keeps_the_half_second_pause(value):
    with pytest.raises(SettingsError, match=r"timing\.mic_stop_grace_s: must be 0\.5 or more seconds"):
        apply_patch(Settings(), {"timing": {"mic_stop_grace_s": value}})


@pytest.mark.parametrize("value", [0.5, 1, 2.5])
def test_stop_grace_of_half_a_second_or_more_is_accepted(value, store):
    s = apply_patch(Settings(), {"timing": {"mic_stop_grace_s": value}})
    assert s.timing.mic_stop_grace_s == value
    store.save(s)
    assert store.load() == s


def test_stop_grace_below_half_a_second_is_refused_in_the_file_and_the_yaml(tmp_path, store):
    store.path.parent.mkdir(parents=True)
    store.path.write_text('{"version": 1, "timing": {"mic_stop_grace_s": 0}}', encoding="utf-8")
    with pytest.raises(SettingsError, match="mic_stop_grace_s"):
        store.load()
    with pytest.raises(SettingsError, match="mic_stop_grace_s"):
        seed(write(tmp_path, "devices: []\ntiming:\n  mic_stop_grace_s: 0.2\n"), ENV)


@pytest.mark.parametrize("name", POSITIVE_TIMINGS)
def test_other_durations_must_be_above_zero(name):
    with pytest.raises(SettingsError, match=f"timing.{name}"):
        apply_patch(Settings(), {"timing": {name: 0}})


LONE = ("\ud800", "\udcff")


@pytest.mark.parametrize(
    ("patch", "where"),
    [
        ({"openai": {"model": "gpt\ud800"}}, "openai.model"),
        ({"openai": {"voice": "\udcffmarin"}}, "openai.voice"),
        ({"openai": {"api_key": "sk-\ud800"}}, "openai.api_key"),
        ({"openai": {"reasoning_effort": "low\ud800"}}, "openai.reasoning_effort"),
        ({"assistant": {"instructions": "Be \ud800 brief."}}, "assistant.instructions"),
        ({"devices": [{"name": "porch\ud800", "host": "192.0.2.71"}]}, "devices[0].name"),
        ({"devices": [{"name": "porch", "host": "192.0.2.\udcff"}]}, "devices[0].host"),
        ({"devices": [{"name": "porch", "host": "192.0.2.71", "key": "\ud800" + KEY_B[1:]}]}, "devices[0].key"),
        ({"advertise_host": "\udcff"}, "advertise_host"),
    ],
)
def test_patch_rejects_text_that_cannot_be_saved(store, patch, where):
    with pytest.raises(SettingsError) as info:
        apply_patch(Settings(), patch)
    message = str(info.value)
    assert where in message
    assert not any(c in message for c in LONE)


def test_damaged_text_in_the_file_is_reported(store):
    store.path.parent.mkdir(parents=True)
    store.path.write_text('{"version": 1, "openai": {"model": "gpt\\ud800"}}', encoding="utf-8")
    with pytest.raises(SettingsError) as info:
        store.load()
    assert "openai.model" in str(info.value) and "\ud800" not in str(info.value)


@pytest.mark.parametrize(
    "settings",
    [
        Settings(openai=OpenAISettings(model="gpt\ud800")),
        Settings(assistant=AssistantSettings(instructions="\udcff")),
        Settings(password_hash="scrypt$\ud800"),
        Settings(devices=(DeviceSettings(name="kitchen", host="192.0.2.50", debug_input_wav="/q\ud800.wav"),)),
    ],
)
def test_save_refuses_unencodable_text_and_leaves_the_file(store, settings):
    store.save(Settings())
    before = store.path.read_bytes()
    with pytest.raises(SettingsError):
        store.save(settings)
    assert store.path.read_bytes() == before
    assert leftovers(store) == []
