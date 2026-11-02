import asyncio
import hmac
import ipaddress
import logging
import re
from collections.abc import Awaitable, Callable, Iterable
from dataclasses import dataclass, field, replace
from typing import Any, Protocol
from urllib.parse import urlsplit

from aiohttp import web

from voice_hub.adopt import AdoptError
from voice_hub.auth import SESSION_TTL_S, Sessions, hash_password, verify_password
from voice_hub.device import VolumeError, WakeWordError
from voice_hub.discovery import Found
from voice_hub.prompts import LANGUAGES
from voice_hub.registry import Hub
from voice_hub.settings import (
    DEFAULT_DEVICE_PORT,
    Settings,
    SettingsError,
    apply_patch,
    check_address,
    check_key,
    public_view,
)

log = logging.getLogger(__name__)

COOKIE = "voice_hub_session"
FAILED_DELAY_S = 1.0
QUEUED_CHECKS = 3
MIN_PASSWORD = 8
MAX_PASSWORD = 1024
MAX_VOLUME = 100
PUBLIC = frozenset({"/api/state", "/api/setup", "/api/login"})
SAFE_METHODS = frozenset({"GET", "HEAD", "OPTIONS"})
OWN_SITE = frozenset({"same-origin", "none"})
JSON_TYPE = "application/json"
LOCAL_SUFFIXES = (".local", ".lan", ".home", ".internal", ".home.arpa")
LABEL = re.compile(r"[a-z0-9_-]+")
HTTP_CODES = {404: "not_found", 405: "method_not_allowed", 413: "too_large"}
SPEAKER_STATUS = {
    "has_key": 409,
    "unsupported": 409,
    "no_key_yet": 409,
    "name_taken": 409,
    "wrong_key": 409,
    "offline": 409,
    "unknown": 409,
    "unknown_speaker": 404,
    "unreachable": 502,
    "no_name": 502,
    "refused": 502,
    "failed": 502,
}


class ApiError(Exception):
    def __init__(self, status: int, code: str, message: str) -> None:
        super().__init__(message)
        self.status = status
        self.code = code


class Finder(Protocol):
    def snapshot(self) -> list[Found]: ...


@dataclass(eq=False)
class _Queue:
    turn: asyncio.Lock = field(default_factory=asyncio.Lock)
    size: int = 0


def _answer(data: Any, status: int = 200) -> web.Response:
    return web.json_response(data, status=status)


def _failure(status: int, code: str, message: str) -> web.Response:
    return _answer({"error": code, "message": message}, status)


def _is_api(path: str) -> bool:
    return path == "/api" or path.startswith("/api/")


def _host_name(host: str) -> str:
    host = host.strip().lower()
    if host.startswith("["):
        host = host[1:].split("]", 1)[0]
    elif host.count(":") == 1:
        host = host.split(":", 1)[0]
    return host.rstrip(".")


def host_names(hosts: Iterable[str]) -> frozenset[str]:
    return frozenset(_host_name(host) for host in hosts if host.strip())


def _local_host(name: str, listed: frozenset[str]) -> bool:
    if not name:
        return False
    if name == "localhost" or name in listed:
        return True
    try:
        ipaddress.ip_address(name)
    except ValueError:
        return LABEL.fullmatch(name) is not None or name.endswith(LOCAL_SUFFIXES)
    return True


def _same_origin(origin: str, host: str) -> bool:
    try:
        parts = urlsplit(origin)
    except ValueError:
        return False
    return parts.scheme in ("http", "https") and bool(parts.netloc) and parts.netloc.lower() == host.lower()


async def _json(request: web.Request) -> Any:
    try:
        return await request.json()
    except (ValueError, LookupError, RecursionError):
        raise ApiError(400, "bad_json", "the body is not valid JSON") from None


async def _object(request: web.Request, allowed: set[str] | None = None) -> dict[str, Any]:
    data = await _json(request)
    if not isinstance(data, dict):
        raise ApiError(400, "bad_json", "the body must be a JSON object")
    unknown = sorted(str(k) for k in data if allowed is not None and k not in allowed)
    if unknown:
        raise ApiError(400, "invalid", f"{', '.join(unknown)}: unknown field")
    return data


def _address(data: dict[str, Any]) -> tuple[str, int]:
    try:
        return check_address(data.get("host"), data.get("port", DEFAULT_DEVICE_PORT))
    except SettingsError as e:
        raise ApiError(400, "invalid", str(e)) from None


def _given(value: Any, where: str) -> str:
    if not isinstance(value, str) or len(value) > MAX_PASSWORD:
        raise ApiError(400, "invalid", f"{where}: must be text of at most {MAX_PASSWORD} characters")
    return value


def _new_password(value: Any, where: str) -> str:
    rule = ApiError(400, "bad_password", f"{where}: must be {MIN_PASSWORD} to {MAX_PASSWORD} characters")
    if not isinstance(value, str) or not MIN_PASSWORD <= len(value) <= MAX_PASSWORD:
        raise rule
    try:
        value.encode("utf-8")
    except UnicodeEncodeError:
        raise rule from None
    return value


def _volume(value: Any) -> int:
    if isinstance(value, float) and value.is_integer():
        value = int(value)
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= MAX_VOLUME:
        raise ApiError(400, "invalid", f"volume: must be a whole number from 0 to {MAX_VOLUME}")
    return value


def _speaker_error(error: AdoptError | WakeWordError | VolumeError) -> ApiError:
    return ApiError(SPEAKER_STATUS.get(error.code, 502), error.code, str(error))


def _device_view(status: dict[str, Any]) -> dict[str, Any]:
    words = status["wake_words"]
    return {
        "name": status["name"],
        "host": status["host"],
        "port": status["port"],
        "key_set": status["key_set"],
        "connected": status["connected"],
        "wake_words": {
            "available": [{"id": wake_id, "phrase": phrase} for wake_id, phrase in words["available"]],
            "active": list(words["active"]),
        },
        "volume": status["volume"],
    }


def _found_view(found: Found, known: set[str], online: set[str]) -> dict[str, Any]:
    return {
        "name": found.name,
        "host": found.host,
        "port": found.port,
        "provisionable": found.provisionable and found.name not in online,
        "friendly_name": found.friendly_name,
        "added": found.name in known,
    }


class Api:
    def __init__(self, hub: Hub, discovery: Finder, password: str | None = None, hosts: Iterable[str] = (),
                 sessions: Sessions | None = None, failed_delay_s: float = FAILED_DELAY_S,
                 env_port: int | None = None) -> None:
        self._hub = hub
        self._discovery = discovery
        self._env_password = password or None
        self._env_port = env_port
        self._hosts = host_names(hosts)
        self._sessions = sessions if sessions is not None else Sessions()
        self._failed_delay_s = failed_delay_s
        self._checking = asyncio.Lock()
        self._queues: dict[str, _Queue] = {}
        self._started = (self._port(hub.settings), hub.settings.advertise_host)

    def install(self, app: web.Application) -> None:
        app.middlewares.append(self._middleware())
        add = app.router
        add.add_get("/api/state", self._state)
        add.add_post("/api/setup", self._setup)
        add.add_post("/api/login", self._login)
        add.add_post("/api/logout", self._logout)
        add.add_post("/api/password", self._change_password)
        add.add_get("/api/settings", self._get_settings)
        add.add_put("/api/settings", self._put_settings)
        add.add_get("/api/devices", self._devices)
        add.add_post("/api/devices", self._add_device)
        add.add_post("/api/devices/adopt", self._adopt)
        add.add_delete("/api/devices/{name}", self._remove_device)
        add.add_put("/api/devices/{name}", self._move_device)
        add.add_put("/api/devices/{name}/wake-word", self._wake_word)
        add.add_put("/api/devices/{name}/volume", self._set_volume)
        add.add_get("/api/discover", self._discover)

    def _middleware(self) -> Callable[..., Awaitable[web.StreamResponse]]:
        @web.middleware
        async def guard(request: web.Request, handler: Callable[[web.Request], Awaitable[web.StreamResponse]]):
            if not _is_api(request.path):
                return await handler(request)
            try:
                self._admit(request)
                response = await handler(request)
            except ApiError as e:
                response = _failure(e.status, e.code, str(e))
            except web.HTTPException as e:
                response = _failure(e.status, HTTP_CODES.get(e.status, "error"), e.reason)
            except Exception:
                log.exception("%s %s failed", request.method, request.path)
                response = _failure(500, "internal", "something went wrong")
            response.headers["Cache-Control"] = "no-store"
            return response

        return guard

    def _admit(self, request: web.Request) -> None:
        if not _local_host(_host_name(request.host), self._hosts):
            raise ApiError(403, "foreign_host", "this server name is not accepted; list it in VOICE_HUB_HOSTS")
        origin = request.headers.get("Origin")
        site = request.headers.get("Sec-Fetch-Site")
        foreign = origin is not None and not _same_origin(origin, request.host)
        if foreign or (site is not None and site not in OWN_SITE):
            raise ApiError(403, "foreign_origin", "requests from other sites are not accepted")
        if request.path not in PUBLIC and not self._sessions.valid(request.cookies.get(COOKIE)):
            raise ApiError(401, "login_required", "log in first")
        if request.method not in SAFE_METHODS and request.content_type != JSON_TYPE:
            raise ApiError(415, "json_required", "send JSON with Content-Type: application/json")

    def _password_set(self) -> bool:
        return self._env_password is not None or self._hub.settings.password_hash is not None

    def _state_view(self, logged_in: bool) -> dict[str, bool]:
        return {"password_set": self._password_set(), "password_from_env": self._env_password is not None,
                "logged_in": logged_in}

    def _port(self, settings: Settings) -> int:
        return self._env_port if self._env_port is not None else settings.http_port

    def _settings_view(self, settings: Settings) -> dict[str, Any]:
        view = public_view(settings)
        view["password_set"] = self._password_set()
        view["http_port"] = self._port(settings)
        view["http_port_from_env"] = self._env_port is not None
        return {"settings": view, "restart_needed": (self._port(settings), settings.advertise_host) != self._started}

    def _devices_view(self) -> list[dict[str, Any]]:
        return [_device_view(status) for status in self._hub.status()]

    def _log_in(self, request: web.Request, body: dict[str, Any]) -> web.Response:
        self._sessions.drop(request.cookies.get(COOKIE))
        response = _answer(body)
        response.set_cookie(COOKIE, self._sessions.create(), max_age=SESSION_TTL_S, path="/", httponly=True,
                            samesite="Strict", secure=request.secure or None)
        return response

    async def _check_password(self, request: web.Request, given: str) -> bool:
        client = request.remote or ""
        queue = self._queues.setdefault(client, _Queue())
        if queue.size > QUEUED_CHECKS:
            raise ApiError(429, "slow_down", "too many password checks from this address at once; wait a few seconds")
        queue.size += 1
        try:
            async with queue.turn:
                ok = await self._verify(given)
                if not ok:
                    log.warning("wrong password from %s", request.remote)
                    await asyncio.sleep(self._failed_delay_s)
                return ok
        finally:
            queue.size -= 1
            if not queue.size:
                del self._queues[client]

    async def _verify(self, given: str) -> bool:
        async with self._checking:
            if self._env_password is not None:
                return hmac.compare_digest(given.encode("utf-8", "surrogatepass"),
                                           self._env_password.encode("utf-8", "surrogatepass"))
            stored = self._hub.settings.password_hash
            return stored is not None and await asyncio.to_thread(verify_password, given, stored)

    async def _save(self, edit: Callable[[Settings], Settings]) -> Settings:
        try:
            return await self._hub.change(edit)
        except SettingsError as e:
            raise ApiError(500, "save_failed", str(e)) from None

    async def _state(self, request: web.Request) -> web.Response:
        return _answer(self._state_view(self._sessions.valid(request.cookies.get(COOKIE))))

    async def _setup(self, request: web.Request) -> web.Response:
        data = await _object(request, {"password", "language"})
        language = data.get("language")
        if "language" in data and language not in LANGUAGES:
            raise ApiError(400, "invalid", f"language: must be one of {', '.join(LANGUAGES)}")
        if self._password_set():
            raise ApiError(409, "password_set", "a password is already set")
        hashed = await asyncio.to_thread(hash_password, _new_password(data.get("password"), "password"))

        def edit(settings: Settings) -> Settings:
            if settings.password_hash is not None:
                raise ApiError(409, "password_set", "a password is already set")
            if language is not None:
                settings = apply_patch(settings, {"assistant": {"language": language}})
            return replace(settings, password_hash=hashed)

        await self._save(edit)
        log.info("the page password was created")
        return self._log_in(request, self._state_view(True))

    async def _login(self, request: web.Request) -> web.Response:
        data = await _object(request, {"password"})
        given = _given(data.get("password"), "password")
        if not self._password_set():
            raise ApiError(409, "no_password", "create a password first")
        if not await self._check_password(request, given):
            raise ApiError(401, "wrong_password", "wrong password")
        return self._log_in(request, self._state_view(True))

    async def _logout(self, request: web.Request) -> web.Response:
        self._sessions.drop(request.cookies.get(COOKIE))
        response = _answer(self._state_view(False))
        response.del_cookie(COOKIE, path="/")
        return response

    async def _change_password(self, request: web.Request) -> web.Response:
        data = await _object(request, {"current", "new"})
        if self._env_password is not None:
            raise ApiError(409, "password_from_env", "the password is set by VOICE_HUB_PASSWORD")
        given = _given(data.get("current"), "current")
        new = _new_password(data.get("new"), "new")
        if not await self._check_password(request, given):
            raise ApiError(403, "wrong_password", "the current password is wrong")
        hashed = await asyncio.to_thread(hash_password, new)
        await self._save(lambda settings: replace(settings, password_hash=hashed))
        self._sessions.clear()
        log.info("the page password was changed")
        return self._log_in(request, self._state_view(True))

    async def _get_settings(self, request: web.Request) -> web.Response:
        return _answer(self._settings_view(self._hub.settings))

    async def _put_settings(self, request: web.Request) -> web.Response:
        patch = await _object(request)
        if "devices" in patch:
            raise ApiError(400, "invalid", "devices: add, adopt and remove speakers with /api/devices")
        if "http_port" in patch and self._env_port is not None:
            raise ApiError(409, "port_from_env", "the port is set by VOICE_HUB_PORT")

        def edit(settings: Settings) -> Settings:
            try:
                return apply_patch(settings, patch)
            except SettingsError as e:
                raise ApiError(400, "invalid", str(e)) from None

        settings = await self._save(edit)
        log.info("settings changed: %s", ", ".join(sorted(patch)))
        return _answer(self._settings_view(settings))

    async def _devices(self, request: web.Request) -> web.Response:
        return _answer({"devices": self._devices_view()})

    async def _add_device(self, request: web.Request) -> web.Response:
        data = await _object(request, {"host", "port", "key"})
        key = data.get("key")
        return await self._take(data, None if isinstance(key, str) and not key.strip() else key)

    async def _adopt(self, request: web.Request) -> web.Response:
        return await self._take(await _object(request, {"host", "port"}), None)

    async def _take(self, data: dict[str, Any], key: Any) -> web.Response:
        host, port = _address(data)
        if key is not None:
            try:
                key = check_key(key)
            except SettingsError as e:
                raise ApiError(400, "invalid", str(e)) from None
        try:
            name = await (self._hub.adopt(host, port) if key is None else self._hub.add(host, port, key))
        except AdoptError as e:
            raise _speaker_error(e) from None
        except SettingsError as e:
            raise ApiError(500, "save_failed", str(e)) from None
        return _answer({"name": name, "devices": self._devices_view()})

    async def _remove_device(self, request: web.Request) -> web.Response:
        name = request.match_info["name"]

        def edit(settings: Settings) -> Settings:
            if not any(device.name == name for device in settings.devices):
                raise ApiError(404, "unknown_speaker", f"no speaker called {name!r}")
            return replace(settings, devices=tuple(d for d in settings.devices if d.name != name))

        await self._save(edit)
        log.info("speaker %s removed from the settings", name)
        return _answer({"devices": self._devices_view()})

    async def _move_device(self, request: web.Request) -> web.Response:
        name = request.match_info["name"]
        data = await _object(request, {"host", "port"})
        host, port = _address(data)

        def edit(settings: Settings) -> Settings:
            device = next((d for d in settings.devices if d.name == name), None)
            if device is None:
                raise ApiError(404, "unknown_speaker", f"no speaker called {name!r}")
            moved = replace(device, host=host, port=port if "port" in data else device.port)
            return replace(settings, devices=tuple(moved if d.name == name else d for d in settings.devices))

        await self._save(edit)
        log.info("speaker %s now at %s", name, host)
        return _answer({"devices": self._devices_view()})

    async def _wake_word(self, request: web.Request) -> web.Response:
        data = await _object(request, {"wake_word"})
        wake_word = data.get("wake_word")
        if not isinstance(wake_word, str) or not wake_word.strip():
            raise ApiError(400, "invalid", "wake_word: must be a non-empty string")
        try:
            await self._hub.set_wake_word(request.match_info["name"], wake_word.strip())
        except WakeWordError as e:
            raise _speaker_error(e) from None
        return _answer({"devices": self._devices_view()})

    async def _set_volume(self, request: web.Request) -> web.Response:
        name = request.match_info["name"]
        percent = _volume((await _object(request, {"volume"})).get("volume"))
        try:
            self._hub.set_volume(name, percent)
        except VolumeError as e:
            raise _speaker_error(e) from None
        log.info("volume on %s set to %d%% from the page", name, percent)
        return _answer({"devices": self._devices_view()})

    async def _discover(self, request: web.Request) -> web.Response:
        known = {device.name for device in self._hub.settings.devices}
        online = {status["name"] for status in self._hub.status() if status["key_set"] and status["connected"]}
        return _answer({"found": [_found_view(found, known, online) for found in self._discovery.snapshot()]})
