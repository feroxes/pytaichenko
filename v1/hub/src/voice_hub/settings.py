import base64
import binascii
import contextlib
import ipaddress
import json
import logging
import math
import os
import re
import tempfile
from collections.abc import Callable, Mapping
from dataclasses import asdict, dataclass, field, fields
from pathlib import Path
from typing import Any

from voice_hub.config import TIMING_INTS, Config, DeviceConfig, OpenAIConfig, Timing, load_config
from voice_hub.prompts import LANGUAGES, default_instructions

log = logging.getLogger(__name__)

SETTINGS_FILE = "settings.json"
VERSION = 1
DEFAULT_LANGUAGE = "uk"
DEFAULT_DEVICE_PORT = 6053
TRANSCRIPTION_MODEL = "gpt-4o-mini-transcribe"
KEY_BYTES = 32
PORT_ENV = "VOICE_HUB_PORT"


class SettingsError(Exception):
    pass


@dataclass(frozen=True)
class OpenAISettings:
    api_key: str | None = field(default=None, repr=False)
    model: str = "gpt-realtime-2.1-mini"
    voice: str = "marin"
    reasoning_effort: str | None = "low"
    vad_threshold: float = 0.5
    silence_ms: int = 600
    log_heard: bool = False


@dataclass(frozen=True)
class AssistantSettings:
    language: str = DEFAULT_LANGUAGE
    instructions: str = default_instructions(DEFAULT_LANGUAGE)


@dataclass(frozen=True)
class DeviceSettings:
    name: str
    host: str
    port: int = DEFAULT_DEVICE_PORT
    key: str | None = field(default=None, repr=False)
    debug_input_wav: str | None = None


@dataclass(frozen=True)
class Settings:
    openai: OpenAISettings = OpenAISettings()
    assistant: AssistantSettings = AssistantSettings()
    timing: Timing = Timing()
    devices: tuple[DeviceSettings, ...] = ()
    password_hash: str | None = field(default=None, repr=False)
    http_port: int = 8080
    advertise_host: str | None = None


TOP_KEYS = {"openai", "assistant", "timing", "devices", "password_hash", "http_port", "advertise_host"}
PATCH_KEYS = TOP_KEYS - {"password_hash"}
DEVICE_PATCH_KEYS = {"name", "host", "port", "key"}
ZERO_TIMINGS = {"mic_holdoff_s"}
MIN_STOP_GRACE_S = 0.5
MIN_SERVER_PORT = 1024
HOST_LABEL = re.compile(r"[A-Za-z0-9_-]{1,63}")
HOST_ZONE = re.compile(r"[A-Za-z0-9_.-]+")
HOST_RULE = "must be a host name or an IP address, without http://, a port or a path"


def _keys(cls: type) -> set[str]:
    return {f.name for f in fields(cls)}


def _object(data: Any, allowed: set[str], where: str) -> dict[str, Any]:
    if not isinstance(data, Mapping):
        raise SettingsError(f"{where or 'settings'}: expected an object")
    unknown = sorted(str(k) for k in data if k not in allowed)
    if unknown:
        prefix = f"{where}." if where else ""
        raise SettingsError(", ".join(f"{prefix}{k}" for k in unknown) + ": unknown setting")
    return dict(data)


def _text(value: Any, where: str, optional: bool = False) -> str | None:
    if value is None and optional:
        return None
    if not isinstance(value, str) or not value.strip():
        raise SettingsError(f"{where}: must be a non-empty string" + (" or null" if optional else ""))
    try:
        value.encode("utf-8")
    except UnicodeEncodeError:
        raise SettingsError(f"{where}: must be valid UTF-8 text") from None
    return value.strip()


def _int(value: Any, where: str, low: int, high: int | None = None) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < low or (high is not None and value > high):
        bounds = f"from {low} to {high}" if high is not None else f"of at least {low}"
        raise SettingsError(f"{where}: must be a whole number {bounds}")
    return value


def _number(value: Any, where: str, check: Callable[[float], bool], rule: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not check(value):
        raise SettingsError(f"{where}: must be {rule}")
    return float(value)


def _port(value: Any, where: str) -> int:
    return _int(value, where, 1, 65535)


def check_server_port(value: Any, where: str = "http_port") -> int:
    return _int(value, where, MIN_SERVER_PORT, 65535)


def _is_host(host: str, zone: bool) -> bool:
    try:
        address = ipaddress.ip_address(host)
    except ValueError:
        name = host.removesuffix(".")
        labels = name.split(".")
        return len(name) <= 253 and all(HOST_LABEL.fullmatch(label) for label in labels) and not labels[-1].isdigit()
    scope = getattr(address, "scope_id", None)
    return scope is None or (zone and HOST_ZONE.fullmatch(scope) is not None)


def _host(value: Any, where: str, optional: bool = False, zone: bool = True) -> str | None:
    host = _text(value, where, optional)
    if host is not None and not _is_host(host, zone):
        raise SettingsError(f"{where}: {HOST_RULE}")
    return host


def _key(value: Any, where: str) -> str | None:
    if value is None:
        return None
    rule = f"{where}: must be base64 of {KEY_BYTES} bytes or null"
    if not isinstance(value, str):
        raise SettingsError(rule)
    try:
        raw = base64.b64decode(value.strip(), validate=True)
    except (binascii.Error, ValueError):
        raise SettingsError(rule) from None
    if len(raw) != KEY_BYTES:
        raise SettingsError(rule)
    return value.strip()


def _openai(data: Any) -> OpenAISettings:
    raw = _object(data, _keys(OpenAISettings), "openai")
    out: dict[str, Any] = {}
    if "api_key" in raw:
        out["api_key"] = _text(raw["api_key"], "openai.api_key", optional=True)
    for name in ("model", "voice"):
        if name in raw:
            out[name] = _text(raw[name], f"openai.{name}")
    if "reasoning_effort" in raw:
        out["reasoning_effort"] = _text(raw["reasoning_effort"], "openai.reasoning_effort", optional=True)
    if "vad_threshold" in raw:
        out["vad_threshold"] = _number(raw["vad_threshold"], "openai.vad_threshold", lambda v: 0 <= v <= 1,
                                       "a number from 0 to 1")
    if "silence_ms" in raw:
        out["silence_ms"] = _int(raw["silence_ms"], "openai.silence_ms", 1)
    if "log_heard" in raw:
        if not isinstance(raw["log_heard"], bool):
            raise SettingsError("openai.log_heard: must be true or false")
        out["log_heard"] = raw["log_heard"]
    return OpenAISettings(**out)


def _language(value: Any, where: str) -> str:
    if value not in LANGUAGES:
        raise SettingsError(f"{where}: must be one of {', '.join(LANGUAGES)}")
    return value


def _assistant(data: Any) -> AssistantSettings:
    raw = _object(data, _keys(AssistantSettings), "assistant")
    out: dict[str, Any] = {}
    if "language" in raw:
        out["language"] = _language(raw["language"], "assistant.language")
    if "instructions" in raw:
        instructions = raw["instructions"]
        _text(instructions, "assistant.instructions")
        out["instructions"] = instructions
    return AssistantSettings(**out)


def _timing(data: Any) -> Timing:
    raw = _object(data, _keys(Timing), "timing")
    out: dict[str, Any] = {}
    for name, value in raw.items():
        where = f"timing.{name}"
        if name in TIMING_INTS:
            out[name] = _int(value, where, 0)
        elif name in ZERO_TIMINGS:
            out[name] = _number(value, where, lambda v: v >= 0, "0 or more seconds")
        elif name == "mic_stop_grace_s":
            out[name] = _number(value, where, lambda v: v >= MIN_STOP_GRACE_S, f"{MIN_STOP_GRACE_S} or more seconds")
        else:
            out[name] = _number(value, where, lambda v: v > 0, "a number of seconds above 0")
    return Timing(**out)


def _device(data: Any, where: str) -> DeviceSettings:
    raw = _object(data, _keys(DeviceSettings), where)
    for name in ("name", "host"):
        if name not in raw:
            raise SettingsError(f"{where}.{name}: required")
    return DeviceSettings(
        name=_text(raw["name"], f"{where}.name"),
        host=_host(raw["host"], f"{where}.host"),
        port=_port(raw.get("port", DEFAULT_DEVICE_PORT), f"{where}.port"),
        key=_key(raw.get("key"), f"{where}.key"),
        debug_input_wav=_text(raw.get("debug_input_wav"), f"{where}.debug_input_wav", optional=True),
    )


def _devices(data: Any) -> tuple[DeviceSettings, ...]:
    if not isinstance(data, list):
        raise SettingsError("devices: expected a list")
    devices: list[DeviceSettings] = []
    for i, item in enumerate(data):
        device = _device(item, f"devices[{i}]")
        if any(d.name == device.name for d in devices):
            raise SettingsError(f"devices[{i}].name: another speaker is already called {device.name!r}")
        devices.append(device)
    return tuple(devices)


def _settings(data: Any) -> Settings:
    raw = _object(data, TOP_KEYS | {"version"}, "")
    if raw.get("version", VERSION) != VERSION:
        raise SettingsError(f"version: only version {VERSION} is supported")
    out: dict[str, Any] = {}
    if "openai" in raw:
        out["openai"] = _openai(raw["openai"])
    if "assistant" in raw:
        out["assistant"] = _assistant(raw["assistant"])
    if "timing" in raw:
        out["timing"] = _timing(raw["timing"])
    if "devices" in raw:
        out["devices"] = _devices(raw["devices"])
    if "password_hash" in raw:
        out["password_hash"] = _text(raw["password_hash"], "password_hash", optional=True)
    if "http_port" in raw:
        out["http_port"] = check_server_port(raw["http_port"])
    if "advertise_host" in raw:
        out["advertise_host"] = _host(raw["advertise_host"], "advertise_host", optional=True, zone=False)
    return Settings(**out)


def to_data(settings: Settings) -> dict[str, Any]:
    return {
        "version": VERSION,
        "openai": asdict(settings.openai),
        "assistant": asdict(settings.assistant),
        "timing": asdict(settings.timing),
        "devices": [asdict(d) for d in settings.devices],
        "password_hash": settings.password_hash,
        "http_port": settings.http_port,
        "advertise_host": settings.advertise_host,
    }


def validate(settings: Settings) -> Settings:
    return _settings(to_data(settings))


class SettingsStore:
    def __init__(self, path: Path) -> None:
        self.path = Path(path)

    def load(self, port: int | None = None) -> Settings | None:
        try:
            text = self.path.read_text(encoding="utf-8")
        except FileNotFoundError:
            return None
        except (OSError, UnicodeDecodeError) as e:
            raise SettingsError(f"{self.path}: cannot be read ({_reason(e)})") from None
        try:
            data = json.loads(text)
        except ValueError as e:
            raise SettingsError(f"{self.path}: damaged ({e})") from None
        if isinstance(data, dict) and "http_port" in data:
            self._check_port(data, port)
        try:
            return _settings(data)
        except SettingsError as e:
            raise SettingsError(f"{self.path}: {e}") from None

    def _check_port(self, data: dict[str, Any], port: int | None) -> None:
        try:
            check_server_port(data["http_port"])
        except SettingsError as e:
            if port is None:
                raise SettingsError(
                    f"{self.path}: {e}; to start, set {PORT_ENV} (with Docker, in .env next to compose.yaml) or fix "
                    "http_port in this file (with Docker it is data/settings.json next to compose.yaml; "
                    "edit it with sudo)"
                ) from None
            log.warning("%s: http_port cannot be used; %s=%d is used and goes into the file with the next change",
                        self.path, PORT_ENV, port)
            data["http_port"] = port

    def save(self, settings: Settings) -> None:
        text = json.dumps(to_data(validate(settings)), ensure_ascii=False, indent=2) + "\n"
        directory = self.path.parent
        try:
            directory.mkdir(mode=0o700, parents=True, exist_ok=True)
            fd, tmp = tempfile.mkstemp(dir=directory, prefix=f".{self.path.name}.", suffix=".tmp")
            try:
                with os.fdopen(fd, "w", encoding="utf-8") as f:
                    f.write(text)
                    f.flush()
                    os.fsync(f.fileno())
                os.replace(tmp, self.path)
            except BaseException:
                with contextlib.suppress(OSError):
                    os.unlink(tmp)
                raise
        except OSError as e:
            raise SettingsError(f"{self.path}: cannot be saved ({_reason(e)})") from None
        _sync_directory(directory)


def _reason(error: Exception) -> str:
    if isinstance(error, OSError) and error.strerror:
        return error.strerror
    return type(error).__name__


def _sync_directory(directory: Path) -> None:
    with contextlib.suppress(OSError):
        fd = os.open(directory, os.O_RDONLY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)


def _patched_assistant(current: AssistantSettings, patch: Any) -> dict[str, Any]:
    raw = _object(patch, _keys(AssistantSettings), "assistant")
    merged = asdict(current) | raw
    language = _language(merged["language"], "assistant.language")
    if "instructions" in raw and raw["instructions"] is None:
        merged["instructions"] = default_instructions(language)
    elif ("instructions" not in raw and language != current.language
          and current.instructions == default_instructions(current.language)):
        merged["instructions"] = default_instructions(language)
    return merged


def _patched_devices(current: tuple[DeviceSettings, ...], patch: Any) -> list[Any]:
    if not isinstance(patch, list):
        raise SettingsError("devices: expected a list")
    known = {d.name: d for d in current}
    merged: list[Any] = []
    for i, item in enumerate(patch):
        raw = _object(item, DEVICE_PATCH_KEYS, f"devices[{i}]")
        name = raw.get("name")
        previous = known.get(name.strip()) if isinstance(name, str) else None
        merged.append((asdict(previous) if previous else {}) | raw)
    return merged


def apply_patch(settings: Settings, patch: Mapping[str, Any]) -> Settings:
    if not isinstance(patch, Mapping):
        raise SettingsError("patch: expected an object")
    raw = _object(patch, PATCH_KEYS, "")
    data = to_data(settings)
    for section in ("openai", "timing"):
        if section in raw:
            data[section] |= _object(raw[section], set(data[section]), section)
    if "assistant" in raw:
        data["assistant"] = _patched_assistant(settings.assistant, raw["assistant"])
    if "devices" in raw:
        data["devices"] = _patched_devices(settings.devices, raw["devices"])
    for name in ("http_port", "advertise_host"):
        if name in raw:
            data[name] = raw[name]
    return _settings(data)


def public_view(settings: Settings) -> dict[str, Any]:
    openai = asdict(settings.openai)
    del openai["api_key"]
    return {
        "openai": {"api_key_set": settings.openai.api_key is not None} | openai,
        "assistant": asdict(settings.assistant),
        "timing": asdict(settings.timing),
        "devices": [
            {"name": d.name, "host": d.host, "port": d.port, "key_set": d.key is not None} for d in settings.devices
        ],
        "password_set": settings.password_hash is not None,
        "http_port": settings.http_port,
        "advertise_host": settings.advertise_host,
    }


def seed(config_path: Path | None, env: Mapping[str, str]) -> Settings:
    if config_path is None:
        return validate(Settings(openai=OpenAISettings(api_key=env.get("OPENAI_API_KEY") or None)))
    cfg = load_config(config_path, env)
    try:
        return validate(_from_config(cfg))
    except SettingsError as e:
        raise SettingsError(f"{config_path}: {e}") from None


def _from_config(cfg: Config) -> Settings:
    openai = cfg.openai
    return Settings(
        openai=OpenAISettings(
            api_key=openai.api_key,
            model=openai.model,
            voice=openai.voice,
            reasoning_effort=openai.reasoning_effort,
            vad_threshold=openai.vad_threshold,
            silence_ms=openai.silence_ms,
            log_heard=openai.transcription_model is not None,
        ),
        assistant=AssistantSettings(),
        timing=cfg.timing,
        devices=tuple(
            DeviceSettings(
                name=d.name,
                host=d.host,
                port=d.port,
                key=d.encryption_key,
                debug_input_wav=str(d.debug_input_wav) if d.debug_input_wav else None,
            )
            for d in cfg.devices
        ),
        http_port=cfg.http_port,
        advertise_host=cfg.advertise_host,
    )


def check_address(host: Any, port: Any) -> tuple[str, int]:
    return _host(host, "host"), _port(port, "port")


def check_key(value: Any) -> str:
    key = _key(value, "key")
    if key is None:
        raise SettingsError(f"key: must be base64 of {KEY_BYTES} bytes")
    return key


def openai_config(settings: Settings) -> OpenAIConfig:
    openai, assistant = settings.openai, settings.assistant
    return OpenAIConfig(
        api_key=openai.api_key,
        model=openai.model,
        voice=openai.voice,
        instructions=assistant.instructions,
        reasoning_effort=openai.reasoning_effort,
        vad_threshold=openai.vad_threshold,
        silence_ms=openai.silence_ms,
        transcription_model=TRANSCRIPTION_MODEL if openai.log_heard else None,
        language=assistant.language,
    )


def device_config(device: DeviceSettings) -> DeviceConfig:
    if device.key is None:
        raise ValueError(f"speaker {device.name} has no key")
    wav = Path(device.debug_input_wav) if device.debug_input_wav else None
    return DeviceConfig(name=device.name, host=device.host, encryption_key=device.key, port=device.port,
                        debug_input_wav=wav)
