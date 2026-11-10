from dataclasses import dataclass, field, fields
from pathlib import Path
from typing import Any, Mapping

import yaml

from voice_hub.prompts import default_instructions


class ConfigError(Exception):
    pass


@dataclass(frozen=True)
class DeviceConfig:
    name: str
    host: str
    encryption_key: str = field(repr=False)
    port: int = 6053
    debug_input_wav: Path | None = None


@dataclass(frozen=True)
class OpenAIConfig:
    api_key: str | None = field(repr=False)
    model: str = "gpt-realtime-2.1-mini"
    voice: str = "marin"
    instructions: str = default_instructions("uk")
    reasoning_effort: str | None = "low"
    vad_threshold: float = 0.5
    silence_ms: int = 600
    transcription_model: str | None = None
    language: str = "uk"
    url: str = "wss://api.openai.com/v1/realtime"


@dataclass(frozen=True)
class Timing:
    first_turn_s: float = 8.0
    follow_up_s: float = 10.0
    max_speech_s: float = 20.0
    response_timeout_s: float = 30.0
    answer_max_s: float = 180.0
    session_idle_s: float = 300.0
    mic_stop_grace_s: float = 0.5
    mic_holdoff_s: float = 0.4
    ready_check_s: float = 2.0
    max_follow_ups: int = 20


@dataclass(frozen=True)
class Config:
    devices: tuple[DeviceConfig, ...]
    openai: OpenAIConfig
    http_port: int = 8080
    advertise_host: str | None = None
    timing: Timing = Timing()


TOP_KEYS = {"http_port", "advertise_host", "devices", "openai", "timing"}
DEVICE_KEYS = {"name", "host", "port", "encryption_key_env", "debug_input_wav"}
OPENAI_KEYS = {"model", "voice", "reasoning_effort", "vad_threshold", "silence_ms", "transcription_model"}
TIMING_KEYS = {f.name for f in fields(Timing)}
TIMING_INTS = {"max_follow_ups"}


def _section(data: Any, allowed: set[str], where: str) -> dict[str, Any]:
    if data is None:
        return {}
    if not isinstance(data, dict):
        raise ConfigError(f"{where}: expected a mapping")
    unknown = sorted(str(k) for k in data if k not in allowed)
    if unknown:
        raise ConfigError(f"{where}: unknown key(s) {', '.join(unknown)}")
    return data


def _secret(env: Mapping[str, str], name: str, where: str) -> str:
    value = env.get(name)
    if not value:
        raise ConfigError(f"{where}: environment variable {name} is not set")
    return value


def _device(raw: Any, index: int, env: Mapping[str, str]) -> DeviceConfig:
    where = f"devices[{index}]"
    data = _section(raw, DEVICE_KEYS, where)
    for required in ("name", "host", "encryption_key_env"):
        if required not in data:
            raise ConfigError(f"{where}: missing key {required}")
    key = _secret(env, data["encryption_key_env"], where)
    wav = data.get("debug_input_wav")
    extra: dict[str, Any] = {}
    if "port" in data:
        extra["port"] = int(data["port"])
    return DeviceConfig(
        name=str(data["name"]),
        host=str(data["host"]),
        encryption_key=key,
        debug_input_wav=Path(wav) if wav else None,
        **extra,
    )


def _openai(raw: Any, env: Mapping[str, str]) -> OpenAIConfig:
    data = dict(_section(raw, OPENAI_KEYS, "openai"))
    for key in ("model", "voice"):
        if key in data and not (isinstance(data[key], str) and data[key].strip()):
            raise ConfigError(f"openai: {key} must be a non-empty string")
    if data.get("reasoning_effort") is not None and not isinstance(data["reasoning_effort"], str):
        raise ConfigError("openai: reasoning_effort must be a string or null")
    model = data.get("transcription_model")
    if model is not None and not (isinstance(model, str) and model.strip()):
        raise ConfigError("openai: transcription_model must be a non-empty string or null")
    if "vad_threshold" in data:
        value = data["vad_threshold"]
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not 0 <= value <= 1:
            raise ConfigError("openai: vad_threshold must be a number from 0 to 1")
        data["vad_threshold"] = float(value)
    if "silence_ms" in data:
        value = data["silence_ms"]
        if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
            raise ConfigError("openai: silence_ms must be a positive integer")
    return OpenAIConfig(api_key=env.get("OPENAI_API_KEY") or None, **data)


def _parse(raw: Any, env: Mapping[str, str]) -> Config:
    top = _section(raw, TOP_KEYS, "config")
    devices_raw = top.get("devices")
    if not isinstance(devices_raw, list):
        raise ConfigError("config: devices must be a list")
    devices = tuple(_device(item, i, env) for i, item in enumerate(devices_raw))

    openai = _openai(top.get("openai"), env)

    timing_raw = _section(top.get("timing"), TIMING_KEYS, "timing")
    timing = Timing(**{k: int(v) if k in TIMING_INTS else float(v) for k, v in timing_raw.items()})

    extra: dict[str, Any] = {}
    if "http_port" in top:
        extra["http_port"] = int(top["http_port"])
    if "advertise_host" in top:
        extra["advertise_host"] = top["advertise_host"]
    return Config(devices=devices, openai=openai, timing=timing, **extra)


def load_config(path: Path, env: Mapping[str, str]) -> Config:
    try:
        raw = yaml.safe_load(Path(path).read_text(encoding="utf-8"))
    except (OSError, yaml.YAMLError) as e:
        raise ConfigError(f"{path}: {e}") from e
    try:
        return _parse(raw, env)
    except (TypeError, ValueError) as e:
        raise ConfigError(f"{path}: {e}") from e
