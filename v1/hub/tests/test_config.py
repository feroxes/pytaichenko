import pytest

from voice_hub.config import ConfigError, DeviceConfig, load_config
from voice_hub.prompts import default_instructions

ENV = {"OPENAI_API_KEY": "sk-test", "DEVICE_API_KEY": "k"}

MINIMAL = (
    "devices:\n"
    "  - name: kitchen\n"
    "    host: 192.0.2.50\n"
    "    encryption_key_env: DEVICE_API_KEY\n"
)


def write(tmp_path, text):
    p = tmp_path / "c.yaml"
    p.write_text(text)
    return p


def test_load_config(tmp_path):
    p = write(
        tmp_path,
        "http_port: 8081\ndevices:\n  - name: kitchen\n    host: 192.0.2.50\n"
        "    encryption_key_env: DEVICE_API_KEY\nopenai:\n  model: gpt-realtime-2.1\n"
        "timing:\n  follow_up_s: 5\n  max_follow_ups: 3\n",
    )
    cfg = load_config(p, ENV)
    assert cfg.http_port == 8081
    assert cfg.devices == (DeviceConfig(name="kitchen", host="192.0.2.50", encryption_key="k"),)
    assert cfg.openai.model == "gpt-realtime-2.1" and cfg.openai.voice == "marin"
    assert cfg.openai.api_key == "sk-test"
    assert cfg.timing.follow_up_s == 5 and cfg.timing.first_turn_s == 8.0 and cfg.timing.session_idle_s == 300.0
    assert cfg.timing.max_follow_ups == 3 and isinstance(cfg.timing.max_follow_ups, int)


def test_defaults(tmp_path):
    cfg = load_config(write(tmp_path, MINIMAL), ENV)
    assert cfg.http_port == 8080
    assert cfg.advertise_host is None
    assert cfg.devices[0].port == 6053
    assert cfg.devices[0].debug_input_wav is None
    assert cfg.openai.model == "gpt-realtime-2.1-mini"
    assert cfg.openai.reasoning_effort == "low"
    assert cfg.openai.vad_threshold == 0.5 and cfg.openai.silence_ms == 600
    assert cfg.openai.transcription_model is None
    assert cfg.openai.url == "wss://api.openai.com/v1/realtime"
    assert cfg.openai.instructions.strip()
    assert cfg.timing.mic_stop_grace_s == 0.5 and cfg.timing.max_speech_s == 20.0
    assert cfg.timing.response_timeout_s == 30.0
    assert cfg.timing.follow_up_s == 10.0 and cfg.timing.max_follow_ups == 20
    assert cfg.timing.session_idle_s == 300.0
    assert cfg.timing.answer_max_s == 180.0 and cfg.timing.ready_check_s == 2.0


def test_default_instructions_are_the_ukrainian_defaults(tmp_path):
    cfg = load_config(write(tmp_path, MINIMAL), ENV).openai
    assert cfg.instructions == default_instructions("uk") and cfg.language == "uk"


def test_optional_fields(tmp_path):
    p = write(
        tmp_path,
        "advertise_host: 192.0.2.60\n"
        "devices:\n  - name: kitchen\n    host: 192.0.2.50\n    port: 6054\n"
        "    encryption_key_env: DEVICE_API_KEY\n    debug_input_wav: /tmp/in.wav\n"
        "openai:\n  voice: cedar\n  reasoning_effort: null\n  vad_threshold: 0.7\n  silence_ms: 400\n"
        "  transcription_model: gpt-4o-mini-transcribe\n",
    )
    cfg = load_config(p, ENV)
    assert cfg.advertise_host == "192.0.2.60"
    assert cfg.devices[0].port == 6054
    assert str(cfg.devices[0].debug_input_wav) == "/tmp/in.wav"
    assert cfg.openai.voice == "cedar" and cfg.openai.reasoning_effort is None
    assert cfg.openai.vad_threshold == 0.7 and cfg.openai.silence_ms == 400
    assert cfg.openai.transcription_model == "gpt-4o-mini-transcribe"


def test_missing_device_key_raises(tmp_path):
    with pytest.raises(ConfigError, match="DEVICE_API_KEY"):
        load_config(write(tmp_path, MINIMAL), {"OPENAI_API_KEY": "sk-test"})


@pytest.mark.parametrize("env", [{"DEVICE_API_KEY": "k"}, {"DEVICE_API_KEY": "k", "OPENAI_API_KEY": ""}])
def test_missing_openai_key_is_left_empty(tmp_path, env):
    assert load_config(write(tmp_path, MINIMAL), env).openai.api_key is None


def test_unknown_key_raises(tmp_path):
    p = write(tmp_path, MINIMAL + "timing:\n  folow_up_s: 5\n")
    with pytest.raises(ConfigError, match="folow_up_s"):
        load_config(p, ENV)


def test_unknown_top_level_key_raises(tmp_path):
    with pytest.raises(ConfigError, match="htp_port"):
        load_config(write(tmp_path, MINIMAL + "htp_port: 1\n"), ENV)


def test_unknown_device_key_raises(tmp_path):
    with pytest.raises(ConfigError, match="hots"):
        load_config(write(tmp_path, MINIMAL + "    hots: x\n"), ENV)


def test_unknown_openai_key_raises(tmp_path):
    with pytest.raises(ConfigError, match="modle"):
        load_config(write(tmp_path, MINIMAL + "openai:\n  modle: x\n"), ENV)


def test_bad_value_type_raises(tmp_path):
    with pytest.raises(ConfigError, match="abc"):
        load_config(write(tmp_path, MINIMAL + "http_port: abc\n"), ENV)


def test_secrets_hidden_from_repr(tmp_path):
    env = {"OPENAI_API_KEY": "sk-openai-secret", "DEVICE_API_KEY": "device-secret-key"}
    cfg = load_config(write(tmp_path, MINIMAL), env)
    text = repr(cfg)
    assert "sk-openai-secret" not in text
    assert "device-secret-key" not in text
    assert "kitchen" in text
    assert cfg.openai.api_key == "sk-openai-secret"
    assert cfg.devices[0].encryption_key == "device-secret-key"


@pytest.mark.parametrize(
    ("key", "value"),
    [
        ("vad_threshold", '"high"'),
        ("vad_threshold", "1.5"),
        ("vad_threshold", "-0.1"),
        ("vad_threshold", "true"),
        ("vad_threshold", ".nan"),
        ("silence_ms", "0"),
        ("silence_ms", "-200"),
        ("silence_ms", "400.5"),
        ("silence_ms", '"600"'),
        ("silence_ms", "true"),
        ("model", '""'),
        ("model", "5"),
        ("model", "null"),
        ("voice", '"  "'),
        ("voice", "[marin]"),
        ("reasoning_effort", "5"),
        ("reasoning_effort", "[low]"),
        ("transcription_model", '""'),
        ("transcription_model", "5"),
    ],
)
def test_bad_openai_value_raises(tmp_path, key, value):
    with pytest.raises(ConfigError, match=key):
        load_config(write(tmp_path, MINIMAL + f"openai:\n  {key}: {value}\n"), ENV)


def test_openai_numbers_are_normalised(tmp_path):
    cfg = load_config(write(tmp_path, MINIMAL + "openai:\n  vad_threshold: 1\n  silence_ms: 500\n"), ENV)
    assert cfg.openai.vad_threshold == 1.0 and isinstance(cfg.openai.vad_threshold, float)
    assert cfg.openai.silence_ms == 500
