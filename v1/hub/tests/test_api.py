import asyncio
import base64
import json
import logging
import time

import aiohttp
import pytest
from aiohttp import web
from aioesphomeapi import (
    ZERO_NOISE_PSK,
    APIConnectionError,
    DeviceInfo,
    EncryptionPlaintextAPIError,
    InvalidEncryptionKeyAPIError,
)

from device_fakes import AdoptClient
from voice_hub import adopt as adopt_module
from voice_hub import api as api_module
from voice_hub import auth
from voice_hub.api import COOKIE, Api
from voice_hub.app import build_app
from voice_hub.auth import SESSION_TTL_S, Sessions, hash_password, verify_password
from voice_hub.device import VolumeError, WakeWordError, WakeWords
from voice_hub.discovery import Found
from voice_hub.prompts import default_instructions
from voice_hub.registry import DeviceRegistry, Hub
from voice_hub.settings import (
    AssistantSettings,
    DeviceSettings,
    OpenAISettings,
    Settings,
    SettingsError,
    SettingsStore,
)
from voice_hub.streams import AudioStreams

PASSWORD = "correct horse battery"
NEW_PASSWORD = "staple of the kitchen"
ENV_PASSWORD = "password from the environment"
WRONG_GUESS = "guess-wrong-123"
SHORT_PASSWORD = "short-1"
FAKE_HASH = "scrypt$1$1$1$AA$AA"
OPENAI_KEY = "sk-MARKER-openai-0123456789abcdef"
NEW_OPENAI_KEY = "sk-MARKER-openai-fedcba9876543210"
JSON = {"Content-Type": "application/json"}


def key(n: int) -> str:
    return base64.b64encode(bytes([n]) * 32).decode("ascii")


KITCHEN = DeviceSettings(name="kitchen", host="192.0.2.10", key=key(1))
HALL = DeviceSettings(name="hall", host="192.0.2.11", key=key(2))
WORDS = WakeWords(available=(("okay_nabu", "Okay Nabu"), ("hey_mycroft", "Hey Mycroft")), active=("okay_nabu",))
FOUND = [
    Found(name="kitchen", host="192.0.2.10", port=6053, provisionable=False, friendly_name="Kitchen"),
    Found(name="speaker-a1b2c3", host="192.0.2.30", port=6053, provisionable=True, friendly_name="Speaker"),
]
ROUTES = {
    ("GET", "/api/state"), ("POST", "/api/setup"), ("POST", "/api/login"), ("POST", "/api/logout"),
    ("POST", "/api/password"), ("GET", "/api/settings"), ("PUT", "/api/settings"), ("GET", "/api/devices"),
    ("POST", "/api/devices"), ("POST", "/api/devices/adopt"), ("DELETE", "/api/devices/{name}"),
    ("PUT", "/api/devices/{name}"), ("PUT", "/api/devices/{name}/wake-word"), ("PUT", "/api/devices/{name}/volume"),
    ("GET", "/api/discover"),
}
PUBLIC = {("GET", "/api/state"), ("POST", "/api/setup"), ("POST", "/api/login")}


class StubLink:
    def __init__(self, device: DeviceSettings, journal: list) -> None:
        self.device = device
        self.name = device.name
        self.connected = False
        self.wake_words = WakeWords()
        self.journal = journal
        self.chosen: list[str] = []
        self.fail: WakeWordError | None = None
        self.level: float | None = 0.5
        self.levels: list[float] = []
        self.volume_fail: VolumeError | None = None

    @property
    def player_volume(self) -> float | None:
        return self.level if self.connected else None

    async def start(self) -> None:
        self.journal.append(("start", self.name))
        self.connected = True
        self.wake_words = WORDS

    async def stop(self) -> None:
        self.journal.append(("stop", self.name))
        self.connected = False

    async def set_wake_word(self, wake_word_id: str) -> None:
        if self.fail is not None:
            raise self.fail
        if wake_word_id not in {known for known, _ in self.wake_words.available}:
            raise WakeWordError(f"{self.name} has no wake word {wake_word_id!r}", "unknown")
        self.chosen.append(wake_word_id)
        self.wake_words = WakeWords(self.wake_words.available, (wake_word_id,))

    def change_volume(self, level: float) -> None:
        if self.volume_fail is not None:
            raise self.volume_fail
        if self.player_volume is None:
            raise VolumeError(f"{self.name} is not connected or has no media player", "offline")
        self.levels.append(level)
        self.level = level


class Links:
    def __init__(self) -> None:
        self.journal: list = []
        self.made: dict[str, StubLink] = {}

    def __call__(self, device: DeviceSettings) -> StubLink:
        link = StubLink(device, self.journal)
        self.made[device.name] = link
        return link


class Speakers:
    def __init__(self) -> None:
        self.client = AdoptClient()
        self.zero: AdoptClient | None = None
        self.args: list[tuple] = []

    def __call__(self, host: str, port: int, key: str) -> AdoptClient:
        self.args.append((host, port, key))
        if key == ZERO_NOISE_PSK and self.zero is not None:
            return self.zero
        return self.client


class FakeDiscovery:
    def __init__(self, found=()) -> None:
        self.found = list(found)

    def snapshot(self) -> list[Found]:
        return list(self.found)


class Clock:
    def __init__(self) -> None:
        self.now = 1000.0

    def __call__(self) -> float:
        return self.now


class GatedClient(AdoptClient):
    def __init__(self) -> None:
        super().__init__()
        self.gate = asyncio.Event()

    async def noise_encryption_set_key(self, key: bytes) -> bool:
        await self.gate.wait()
        return await super().noise_encryption_set_key(key)


@web.middleware
async def remote_from_header(request, handler):
    remote = request.headers.get("X-Test-Remote")
    return await handler(request.clone(remote=remote) if remote else request)


class World:
    def __init__(self, store: SettingsStore, hub: Hub, links: Links, speakers: Speakers, discovery: FakeDiscovery,
                 client) -> None:
        self.store = store
        self.hub = hub
        self.links = links
        self.speakers = speakers
        self.discovery = discovery
        self.client = client

    @property
    def app(self):
        return self.client.server.app

    async def call(self, method: str, path: str, body=None, headers=None, **kw):
        data = None if body is None else body if isinstance(body, str) else json.dumps(body)
        return await self.client.request(method, path, data=data, headers=JSON | (headers or {}), **kw)

    async def login(self, password: str = PASSWORD):
        resp = await self.call("POST", "/api/login", {"password": password})
        assert resp.status == 200
        return resp

    def forget_cookies(self) -> None:
        self.client.session.cookie_jar.clear()


@pytest.fixture(autouse=True)
def quick(monkeypatch):
    monkeypatch.setattr(auth, "SCRYPT_N", 2 ** 8)
    monkeypatch.setattr(adopt_module, "PROBE_DELAY_S", 0.0)


@pytest.fixture
async def make(tmp_path, aiohttp_client):
    hubs: list[Hub] = []

    async def make(settings: Settings | None = None, password: str | None = None, delay: float = 0.0,
                   clock=None, found=FOUND, hosts=(), env_port: int | None = None) -> World:
        if settings is None:
            settings = standard()
        store = SettingsStore(tmp_path / f"world{len(hubs)}" / "settings.json")
        store.save(settings)
        links, speakers, discovery = Links(), Speakers(), FakeDiscovery(found)
        hub = Hub(store, settings, DeviceRegistry(links), client_factory=speakers)
        hubs.append(hub)
        await hub.start()
        sessions = Sessions(clock=clock) if clock is not None else Sessions()
        api = Api(hub, discovery, password=password, hosts=hosts, sessions=sessions, failed_delay_s=delay,
                  env_port=env_port)
        app = build_app(AudioStreams(), hub.registry.health, api)
        app.middlewares.insert(0, remote_from_header)
        client = await aiohttp_client(app)
        return World(store, hub, links, speakers, discovery, client)

    yield make
    for hub in hubs:
        await hub.close()


def standard(**kw) -> Settings:
    values = {"openai": OpenAISettings(api_key=OPENAI_KEY), "devices": (KITCHEN, HALL),
              "password_hash": hash_password(PASSWORD)} | kw
    return Settings(**values)


def api_routes(app) -> set[tuple[str, str]]:
    return {(route.method, route.resource.canonical) for route in app.router.routes()
            if route.resource is not None and route.resource.canonical.startswith("/api") and route.method != "HEAD"}


def concrete(path: str) -> str:
    return path.replace("{name}", "kitchen")


def cookie_header(resp) -> str:
    (header,) = resp.headers.getall("Set-Cookie")
    return header


async def test_the_api_has_the_documented_routes(make):
    w = await make()
    assert api_routes(w.app) == ROUTES


async def test_every_private_route_needs_a_login(make):
    w = await make()
    for method, path in sorted(ROUTES - PUBLIC):
        resp = await w.call(method, concrete(path), {})
        assert resp.status == 401, (method, path)
        assert await resp.json() == {"error": "login_required", "message": "log in first"}
    for method, path in [("GET", "/api/nope"), ("GET", "/api"), ("POST", "/api/devices/kitchen")]:
        assert (await w.call(method, path, {})).status == 401
    w.client.session.cookie_jar.update_cookies({COOKIE: "forged-token"})
    assert (await w.call("GET", "/api/settings")).status == 401
    assert w.links.journal == [("start", "kitchen"), ("start", "hall")] and w.speakers.args == []


async def test_state_tells_which_screen_to_show(make):
    w = await make(standard(password_hash=None))
    resp = await w.call("GET", "/api/state")
    assert resp.status == 200
    assert await resp.json() == {"password_set": False, "password_from_env": False, "logged_in": False}
    w = await make()
    assert (await (await w.call("GET", "/api/state")).json())["password_set"] is True
    await w.login()
    assert await (await w.call("GET", "/api/state")).json() == {
        "password_set": True, "password_from_env": False, "logged_in": True}


async def test_setup_creates_the_password_and_logs_in(make):
    w = await make(standard(password_hash=None))
    resp = await w.call("POST", "/api/setup", {"password": PASSWORD})
    assert resp.status == 200
    assert await resp.json() == {"password_set": True, "password_from_env": False, "logged_in": True}
    stored = w.store.load().password_hash
    assert stored == w.hub.settings.password_hash and verify_password(PASSWORD, stored)
    assert PASSWORD not in w.store.path.read_text()
    assert (await w.call("GET", "/api/settings")).status == 200


async def test_setup_takes_the_page_language_for_the_conversation(make):
    w = await make(standard(password_hash=None))
    resp = await w.call("POST", "/api/setup", {"password": PASSWORD, "language": "en"})
    assert resp.status == 200
    for settings in (w.store.load(), w.hub.settings):
        assert settings.assistant == AssistantSettings(language="en", instructions=default_instructions("en"))


async def test_setup_keeps_own_instructions_when_it_sets_the_language(make):
    own = AssistantSettings(instructions="Відповідай віршем.")
    w = await make(standard(password_hash=None, assistant=own))
    resp = await w.call("POST", "/api/setup", {"password": PASSWORD, "language": "en"})
    assert resp.status == 200
    assert w.store.load().assistant == AssistantSettings(language="en", instructions="Відповідай віршем.")


async def test_setup_without_a_language_leaves_the_conversation_as_it_is(make):
    w = await make(standard(password_hash=None))
    resp = await w.call("POST", "/api/setup", {"password": PASSWORD})
    assert resp.status == 200
    assert w.store.load().assistant == AssistantSettings()


async def test_setup_works_only_while_there_is_no_password(make):
    w = await make()
    before = w.store.path.read_bytes()
    resp = await w.call("POST", "/api/setup", {"password": NEW_PASSWORD})
    assert resp.status == 409 and (await resp.json())["error"] == "password_set"
    assert "Set-Cookie" not in resp.headers and w.store.path.read_bytes() == before
    assert (await w.call("GET", "/api/settings")).status == 401


async def test_setup_is_closed_when_the_password_comes_from_the_environment(make):
    w = await make(standard(password_hash=None), password=ENV_PASSWORD)
    assert await (await w.call("GET", "/api/state")).json() == {
        "password_set": True, "password_from_env": True, "logged_in": False}
    resp = await w.call("POST", "/api/setup", {"password": PASSWORD})
    assert resp.status == 409 and (await resp.json())["error"] == "password_set"
    assert w.store.load().password_hash is None


@pytest.mark.parametrize("body, code", [
    ({"password": "short"}, "bad_password"), ({"password": ""}, "bad_password"),
    ({"password": 12345678}, "bad_password"), ({"password": None}, "bad_password"), ({}, "bad_password"),
    ({"password": "x" * 1025}, "bad_password"),
    ({"password": "\ud800" * 8}, "bad_password"), ({"password": PASSWORD, "hint": "x"}, "invalid"),
    ({"password": PASSWORD, "language": "de"}, "invalid"), ({"password": PASSWORD, "language": None}, "invalid"),
    ({"password": PASSWORD, "language": ["en"]}, "invalid"),
    ("[]", "bad_json"), ("{", "bad_json"), ("", "bad_json"),
])
async def test_setup_checks_the_password(make, body, code):
    w = await make(standard(password_hash=None))
    resp = await w.call("POST", "/api/setup", body)
    assert resp.status == 400 and (await resp.json())["error"] == code
    assert "Set-Cookie" not in resp.headers and w.store.load().password_hash is None


async def test_two_setups_at_once_make_one_password(make):
    w = await make(standard(password_hash=None))
    first, second = await asyncio.gather(w.call("POST", "/api/setup", {"password": PASSWORD}),
                                         w.call("POST", "/api/setup", {"password": NEW_PASSWORD}))
    assert sorted([first.status, second.status]) == [200, 409]
    winner = PASSWORD if first.status == 200 else NEW_PASSWORD
    stored = w.store.load().password_hash
    assert verify_password(winner, stored) and not verify_password({PASSWORD, NEW_PASSWORD}.difference({winner}).pop(),
                                                                    stored)


async def test_login_with_the_right_password(make):
    w = await make()
    resp = await w.login()
    assert await resp.json() == {"password_set": True, "password_from_env": False, "logged_in": True}
    assert (await w.call("GET", "/api/settings")).status == 200


async def test_the_session_cookie_is_http_only_and_strict(make):
    w = await make()
    resp = await w.login()
    header = cookie_header(resp)
    parts = [part.strip() for part in header.split(";")]
    name, token = parts[0].split("=", 1)
    assert name == COOKIE and len(token) >= 40
    assert "HttpOnly" in parts and "SameSite=Strict" in parts and "Path=/" in parts
    assert f"Max-Age={SESSION_TTL_S}" in parts and "Secure" not in parts


async def test_a_wrong_password_is_refused_after_a_pause(make, caplog):
    w = await make(delay=0.2)
    started = time.monotonic()
    resp = await w.call("POST", "/api/login", {"password": "guess-123-xyz"})
    assert time.monotonic() - started >= 0.2
    assert resp.status == 401 and await resp.json() == {"error": "wrong_password", "message": "wrong password"}
    assert "Set-Cookie" not in resp.headers
    assert (await w.call("GET", "/api/settings")).status == 401
    assert "wrong password" in caplog.text and "guess-123-xyz" not in caplog.text


async def test_password_checks_from_one_address_wait_their_turn(make):
    w = await make(delay=0.2)
    started = time.monotonic()
    replies = await asyncio.gather(*(w.call("POST", "/api/login", {"password": f"guess {i}"}) for i in range(3)))
    assert time.monotonic() - started >= 0.6
    assert [r.status for r in replies] == [401, 401, 401]
    assert all("Set-Cookie" not in r.headers for r in replies)
    await w.login()


async def test_a_flood_from_one_address_beyond_the_queue_is_turned_away(make):
    w = await make(delay=0.3)
    started = time.monotonic()

    async def guess(i):
        resp = await w.call("POST", "/api/login", {"password": f"guess {i}"})
        return resp.status, (await resp.json())["error"], time.monotonic() - started

    results = await asyncio.gather(*(guess(i) for i in range(6)))
    assert sorted(status for status, _, _ in results) == [401, 401, 401, 401, 429, 429]
    assert all(error == "wrong_password" for status, error, _ in results if status == 401)
    assert all(error == "slow_down" and took < 0.25 for status, error, took in results if status == 429)
    assert max(took for _, _, took in results) >= 1.2
    assert (await w.call("POST", "/api/login", {"password": "guess 7"})).status == 401
    await w.login()


async def test_a_flood_from_one_address_does_not_hold_up_other_addresses(make):
    w = await make(delay=0.4)
    flood = [asyncio.create_task(w.call("POST", "/api/login", {"password": f"guess {i}"},
                                        headers={"X-Test-Remote": "192.0.2.50"})) for i in range(6)]
    await asyncio.sleep(0.1)
    started = time.monotonic()
    right = await w.call("POST", "/api/login", {"password": PASSWORD}, headers={"X-Test-Remote": "192.0.2.52"})
    assert right.status == 200 and time.monotonic() - started < 0.3
    started = time.monotonic()
    other = await w.call("POST", "/api/login", {"password": "guess-2"}, headers={"X-Test-Remote": "192.0.2.51"})
    assert other.status == 401 and 0.4 <= time.monotonic() - started < 0.7
    assert sorted([(await t).status for t in flood]) == [401, 401, 401, 401, 429, 429]


async def test_the_owner_gets_in_past_a_guesser_on_the_same_address(make):
    w = await make(delay=0.3)
    stop = asyncio.Event()
    guesses: list[int] = []

    async def guess():
        while not stop.is_set():
            guesses.append((await w.call("POST", "/api/login", {"password": WRONG_GUESS})).status)

    guesser = asyncio.create_task(guess())
    try:
        for _ in range(3):
            await asyncio.sleep(0.1)
            started = time.monotonic()
            resp = await w.call("POST", "/api/login", {"password": PASSWORD})
            assert resp.status == 200 and time.monotonic() - started < 0.8
    finally:
        stop.set()
        await guesser
    assert set(guesses) == {401} and len(guesses) >= 3


async def test_a_password_change_waits_behind_a_guess_from_its_address(make):
    w = await make(delay=0.3)
    await w.login()
    guess = asyncio.create_task(w.call("POST", "/api/login", {"password": "guess-1"}))
    await asyncio.sleep(0.05)
    started = time.monotonic()
    resp = await w.call("POST", "/api/password", {"current": PASSWORD, "new": NEW_PASSWORD})
    assert resp.status == 200 and time.monotonic() - started >= 0.2
    assert (await guess).status == 401


async def test_abandoned_password_checks_free_their_places(make):
    w = await make(delay=5.0)
    pending = [asyncio.create_task(w.call("POST", "/api/login", {"password": f"guess {i}"})) for i in range(4)]
    await asyncio.sleep(0.2)
    refused = await w.call("POST", "/api/login", {"password": "guess 5"})
    assert refused.status == 429 and (await refused.json())["error"] == "slow_down"
    for task in pending:
        task.cancel()
    await asyncio.gather(*pending, return_exceptions=True)
    await asyncio.sleep(0.1)
    started = time.monotonic()
    replies = await asyncio.gather(*(w.call("POST", "/api/login", {"password": PASSWORD}) for _ in range(4)))
    assert [r.status for r in replies] == [200] * 4 and time.monotonic() - started < 1.0


async def test_a_failed_password_check_frees_its_place(make, monkeypatch):
    w = await make()

    def broken(given, stored):
        raise RuntimeError("hash failed")

    monkeypatch.setattr(api_module, "verify_password", broken)
    replies = await asyncio.gather(*(w.call("POST", "/api/login", {"password": PASSWORD}) for _ in range(4)))
    assert [r.status for r in replies] == [500] * 4
    monkeypatch.setattr(api_module, "verify_password", verify_password)
    replies = await asyncio.gather(*(w.call("POST", "/api/login", {"password": PASSWORD}) for _ in range(4)))
    assert [r.status for r in replies] == [200] * 4


async def test_login_before_a_password_exists(make):
    w = await make(standard(password_hash=None))
    resp = await w.call("POST", "/api/login", {"password": PASSWORD})
    assert resp.status == 409 and (await resp.json())["error"] == "no_password"


async def test_odd_bodies_are_bad_json_not_a_failure(make, caplog):
    w = await make()
    for path in ("/api/login", "/api/setup"):
        resp = await w.client.post(path, data=b'{"password": "x"}',
                                   headers={"Content-Type": "application/json; charset=x-nope"})
        assert resp.status == 400 and (await resp.json())["error"] == "bad_json"
        resp = await w.call("POST", path, "[" * 100000 + "]" * 100000)
        assert resp.status == 400 and (await resp.json())["error"] == "bad_json"
    assert "Traceback" not in caplog.text and "failed" not in caplog.text


@pytest.mark.parametrize("body, status, code", [
    ({"password": 5}, 400, "invalid"), ({}, 400, "invalid"), ({"password": "x" * 1025}, 400, "invalid"),
    ("[1]", 400, "bad_json"), ("nope", 400, "bad_json"), ({"password": "\ud800"}, 401, "wrong_password"),
])
async def test_login_checks_its_body(make, body, status, code):
    w = await make()
    resp = await w.call("POST", "/api/login", body)
    assert resp.status == status and (await resp.json())["error"] == code


async def test_the_password_from_the_environment_wins(make):
    w = await make(password=ENV_PASSWORD)
    assert (await w.call("POST", "/api/login", {"password": PASSWORD})).status == 401
    await w.login(ENV_PASSWORD)
    assert (await w.call("GET", "/api/settings")).status == 200


async def test_logout_ends_the_session(make):
    w = await make()
    resp = await w.login()
    token = cookie_header(resp).split(";")[0].split("=", 1)[1]
    resp = await w.call("POST", "/api/logout")
    assert resp.status == 200
    assert await resp.json() == {"password_set": True, "password_from_env": False, "logged_in": False}
    header = cookie_header(resp)
    assert header.startswith(f"{COOKIE}=") and "Max-Age=0" in header
    assert (await w.call("GET", "/api/settings")).status == 401
    w.client.session.cookie_jar.update_cookies({COOKIE: token})
    assert (await w.call("GET", "/api/settings")).status == 401


async def test_sessions_end_after_thirty_days(make):
    clock = Clock()
    w = await make(clock=clock)
    await w.login()
    clock.now += SESSION_TTL_S - 10
    assert (await w.call("GET", "/api/settings")).status == 200
    clock.now += 20
    assert (await w.call("GET", "/api/settings")).status == 401


async def test_password_change_ends_other_sessions(make):
    w = await make()
    async with aiohttp.ClientSession(cookie_jar=aiohttp.CookieJar(unsafe=True)) as other:
        resp = await other.post(w.client.make_url("/api/login"), json={"password": PASSWORD})
        assert resp.status == 200
        await w.login()
        resp = await w.call("POST", "/api/password", {"current": PASSWORD, "new": NEW_PASSWORD})
        assert resp.status == 200 and (await resp.json())["logged_in"] is True
        assert COOKIE in cookie_header(resp)
        assert verify_password(NEW_PASSWORD, w.store.load().password_hash)
        assert (await w.call("GET", "/api/settings")).status == 200
        assert (await other.get(w.client.make_url("/api/settings"))).status == 401
    w.forget_cookies()
    assert (await w.call("POST", "/api/login", {"password": PASSWORD})).status == 401
    await w.login(NEW_PASSWORD)


@pytest.mark.parametrize("body, status, code", [
    ({"current": "wrong password", "new": NEW_PASSWORD}, 403, "wrong_password"),
    ({"current": PASSWORD, "new": "short"}, 400, "bad_password"),
    ({"current": PASSWORD}, 400, "bad_password"),
    ({"current": PASSWORD, "new": NEW_PASSWORD, "again": NEW_PASSWORD}, 400, "invalid"),
])
async def test_password_change_checks_both_passwords(make, body, status, code):
    w = await make(delay=0.05)
    await w.login()
    before = w.store.path.read_bytes()
    started = time.monotonic()
    resp = await w.call("POST", "/api/password", body)
    assert resp.status == status and (await resp.json())["error"] == code
    if code == "wrong_password":
        assert time.monotonic() - started >= 0.05
    assert w.store.path.read_bytes() == before and (await w.call("GET", "/api/settings")).status == 200


async def test_password_from_the_environment_cannot_be_changed_here(make):
    w = await make(password=ENV_PASSWORD)
    await w.login(ENV_PASSWORD)
    resp = await w.call("POST", "/api/password", {"current": ENV_PASSWORD, "new": NEW_PASSWORD})
    assert resp.status == 409 and (await resp.json())["error"] == "password_from_env"


@pytest.mark.parametrize("origin", ["http://evil.test", "null", "http://127.0.0.1:1", "https://evil.test", "nonsense"])
@pytest.mark.parametrize("method, path, body", [
    ("GET", "/api/state", None), ("POST", "/api/login", {"password": PASSWORD}),
    ("POST", "/api/setup", {"password": NEW_PASSWORD}), ("GET", "/api/settings", None),
    ("PUT", "/api/settings", {"openai": {"model": "gpt-x"}}), ("DELETE", "/api/devices/kitchen", None),
    ("POST", "/api/devices/adopt", {"host": "192.0.2.30"}), ("POST", "/api/logout", None),
])
async def test_requests_from_another_origin_are_refused(make, origin, method, path, body):
    w = await make()
    await w.login()
    before = w.store.path.read_bytes()
    resp = await w.call(method, path, body, headers={"Origin": origin})
    assert resp.status == 403 and (await resp.json())["error"] == "foreign_origin"
    assert "Set-Cookie" not in resp.headers
    assert w.store.path.read_bytes() == before and w.speakers.args == []
    assert ("stop", "kitchen") not in w.links.journal
    assert (await w.call("GET", "/api/settings")).status == 200


@pytest.mark.parametrize("host", ["evil.test", "evil.test:8080", "hub.lan.evil.test", "192.0.2.5.nip.io",
                                  "localhost.evil.test", "[evil.test]", "hub%2eevil", ""])
@pytest.mark.parametrize("method, path, body", [
    ("GET", "/api/state", None), ("POST", "/api/setup", {"password": PASSWORD}),
    ("POST", "/api/login", {"password": PASSWORD}),
])
async def test_requests_for_a_foreign_host_name_are_refused(make, host, method, path, body):
    w = await make(standard(password_hash=None))
    resp = await w.call(method, path, body, headers={"Host": host})
    assert resp.status == 403 and (await resp.json())["error"] == "foreign_host"
    assert "Set-Cookie" not in resp.headers and w.store.load().password_hash is None


async def test_a_session_does_not_help_a_foreign_host_name(make):
    w = await make()
    await w.login()
    before = w.store.path.read_bytes()
    for method, path, body in [("GET", "/api/settings", None), ("PUT", "/api/settings", {"http_port": 8181}),
                               ("DELETE", "/api/devices/kitchen", None)]:
        resp = await w.call(method, path, body, headers={"Host": "evil.test:8080"})
        assert resp.status == 403 and (await resp.json())["error"] == "foreign_host"
    assert w.store.path.read_bytes() == before


@pytest.mark.parametrize("host", [
    "192.0.2.5", "192.0.2.5:8080", "[::1]:8080", "[fe80::1]", "[::ffff:192.0.2.5]:8080", "::1", "localhost",
    "localhost:8080", "LocalHost", "hub", "HUB:8080", "my-hub_2", "hub.lan", "hub.lan.", "hub.local", "hub.home",
    "hub.internal", "hub.home.arpa:8080",
])
async def test_local_host_names_are_accepted(make, host):
    w = await make()
    resp = await w.call("GET", "/api/state", headers={"Host": host})
    assert resp.status == 200
    await w.call("POST", "/api/login", {"password": PASSWORD}, headers={"Host": host})
    assert (await w.call("GET", "/api/settings", headers={"Host": host})).status == 200


@pytest.mark.parametrize("host, status", [
    ("voice.example.com", 200), ("VOICE.example.COM:8443", 200), ("other.example.org", 200),
    ("example.com", 403), ("voice.example.com.evil.test", 403),
])
async def test_listed_host_names_are_accepted(make, host, status):
    w = await make(hosts=["Voice.Example.com", " other.example.org ", ""])
    assert (await w.call("GET", "/api/state", headers={"Host": host})).status == status
    unlisted = await make()
    assert (await unlisted.call("GET", "/api/state", headers={"Host": "voice.example.com"})).status == 403


@pytest.mark.parametrize("site", ["cross-site", "same-site"])
async def test_fetch_metadata_from_another_site_is_refused(make, site):
    w = await make()
    await w.login()
    resp = await w.call("PUT", "/api/settings", {"openai": {"model": "gpt-x"}}, headers={"Sec-Fetch-Site": site})
    assert resp.status == 403 and w.hub.settings.openai.model != "gpt-x"


async def test_requests_from_the_page_itself_are_accepted(make):
    w = await make()
    origin = f"http://{w.client.host}:{w.client.port}"
    headers = {"Origin": origin, "Sec-Fetch-Site": "same-origin"}
    assert (await w.call("POST", "/api/login", {"password": PASSWORD}, headers=headers)).status == 200
    resp = await w.call("PUT", "/api/settings", {"openai": {"model": "gpt-x"}}, headers=headers)
    assert resp.status == 200 and w.hub.settings.openai.model == "gpt-x"


@pytest.mark.parametrize("method, path, data, content_type", [
    ("PUT", "/api/settings", '{"openai": {"model": "gpt-x"}}', "text/plain"),
    ("PUT", "/api/settings", "openai=1", "application/x-www-form-urlencoded"),
    ("DELETE", "/api/devices/kitchen", None, None),
    ("POST", "/api/logout", None, "text/plain"),
    ("POST", "/api/devices/adopt", '{"host": "192.0.2.30"}', "multipart/form-data; boundary=x"),
])
async def test_changes_must_be_sent_as_json(make, method, path, data, content_type):
    w = await make()
    await w.login()
    before = w.store.path.read_bytes()
    headers = {"Content-Type": content_type} if content_type else {}
    resp = await w.client.request(method, path, data=data, headers=headers, skip_auto_headers={"Content-Type"})
    assert resp.status == 415 and (await resp.json())["error"] == "json_required"
    assert w.store.path.read_bytes() == before and w.speakers.args == []
    assert (await w.call("GET", "/api/settings")).status == 200


async def test_login_from_a_form_is_refused(make):
    w = await make()
    resp = await w.client.post("/api/login", data={"password": PASSWORD})
    assert resp.status == 415 and "Set-Cookie" not in resp.headers


async def test_health_and_audio_need_no_login(make):
    w = await make()
    for headers in ({"Origin": "http://evil.test"}, {"Host": "evil.test"}):
        resp = await w.client.get("/health", headers=headers)
        assert resp.status == 200 and await resp.json() == {"devices": {"kitchen": True, "hall": True}}
        resp = await w.client.get("/audio/nope.wav", headers=headers)
        assert resp.status == 404 and resp.content_type != "application/json"


async def test_api_answers_are_not_cached(make):
    w = await make()
    for resp in (await w.call("GET", "/api/state"), await w.call("GET", "/api/settings"), await w.login(),
                 await w.call("GET", "/api/settings")):
        assert resp.headers["Cache-Control"] == "no-store"


async def test_unknown_api_routes_and_methods_answer_in_json(make):
    w = await make()
    await w.login()
    resp = await w.call("GET", "/api/nope")
    assert resp.status == 404 and (await resp.json())["error"] == "not_found"
    resp = await w.call("PATCH", "/api/settings", {})
    assert resp.status == 405 and (await resp.json())["error"] == "method_not_allowed"


async def test_unexpected_errors_say_nothing_more(make, monkeypatch, caplog):
    w = await make()
    await w.login()

    def broken():
        raise RuntimeError("internal detail")

    monkeypatch.setattr(w.hub, "status", broken)
    resp = await w.call("GET", "/api/devices")
    assert resp.status == 500
    assert await resp.json() == {"error": "internal", "message": "something went wrong"}
    assert "internal detail" in caplog.text


async def test_settings_show_what_is_set_but_not_the_secrets(make):
    w = await make()
    await w.login()
    resp = await w.call("GET", "/api/settings")
    body = await resp.json()
    assert set(body) == {"settings", "restart_needed"} and body["restart_needed"] is False
    view = body["settings"]
    assert set(view) == {"openai", "assistant", "timing", "devices", "password_set", "http_port", "http_port_from_env",
                         "advertise_host"}
    assert set(view["openai"]) == {"api_key_set", "model", "voice", "reasoning_effort", "vad_threshold", "silence_ms",
                                   "log_heard"}
    assert set(view["assistant"]) == {"language", "instructions"}
    assert set(view["timing"]) == {"first_turn_s", "follow_up_s", "max_speech_s", "response_timeout_s", "answer_max_s",
                                   "session_idle_s", "mic_stop_grace_s", "mic_holdoff_s", "ready_check_s",
                                   "max_follow_ups"}
    assert all(set(device) == {"name", "host", "port", "key_set"} for device in view["devices"])
    assert view["openai"]["api_key_set"] is True
    assert view["openai"]["model"] == "gpt-realtime-2.1-mini" and view["assistant"]["language"] == "uk"
    assert view["timing"]["follow_up_s"] == w.hub.settings.timing.follow_up_s
    assert view["devices"] == [{"name": "kitchen", "host": "192.0.2.10", "port": 6053, "key_set": True},
                               {"name": "hall", "host": "192.0.2.11", "port": 6053, "key_set": True}]
    assert view["password_set"] is True and "password_hash" not in view
    assert view["http_port"] == 8080 and view["http_port_from_env"] is False and view["advertise_host"] is None


async def test_settings_say_the_password_is_set_when_it_comes_from_the_environment(make):
    w = await make(standard(password_hash=None), password=ENV_PASSWORD)
    await w.login(ENV_PASSWORD)
    assert (await (await w.call("GET", "/api/settings")).json())["settings"]["password_set"] is True


async def test_put_settings_changes_and_saves(make):
    w = await make()
    await w.login()
    patch = {"openai": {"model": "gpt-x", "api_key": NEW_OPENAI_KEY}, "timing": {"follow_up_s": 7.5},
             "assistant": {"language": "en"}}
    resp = await w.call("PUT", "/api/settings", patch)
    assert resp.status == 200
    body = await resp.json()
    assert body["restart_needed"] is False and body["settings"]["openai"]["model"] == "gpt-x"
    assert body["settings"]["assistant"]["language"] == "en" and body["settings"]["timing"]["follow_up_s"] == 7.5
    saved = w.store.load()
    assert saved == w.hub.settings
    assert saved.openai.api_key == NEW_OPENAI_KEY and saved.openai.model == "gpt-x" and saved.timing.follow_up_s == 7.5
    assert NEW_OPENAI_KEY not in await resp.text()
    assert [link.name for link in w.hub.registry] == ["kitchen", "hall"] and len(w.links.journal) == 2


async def test_put_settings_can_clear_the_openai_key(make):
    w = await make()
    await w.login()
    resp = await w.call("PUT", "/api/settings", {"openai": {"api_key": None}})
    assert (await resp.json())["settings"]["openai"]["api_key_set"] is False
    assert w.store.load().openai.api_key is None


async def test_restart_is_needed_only_after_a_port_or_address_change(make):
    w = await make()
    await w.login()
    resp = await w.call("PUT", "/api/settings", {"http_port": 8181})
    assert (await resp.json())["restart_needed"] is True
    assert (await (await w.call("GET", "/api/settings")).json())["restart_needed"] is True
    resp = await w.call("PUT", "/api/settings", {"http_port": 8080})
    assert (await resp.json())["restart_needed"] is False
    resp = await w.call("PUT", "/api/settings", {"advertise_host": "192.0.2.5"})
    assert (await resp.json())["restart_needed"] is True
    resp = await w.call("PUT", "/api/settings", {"advertise_host": None, "openai": {"voice": "cedar"}})
    assert (await resp.json())["restart_needed"] is False


async def test_the_port_from_the_environment_is_shown_and_cannot_be_changed_here(make):
    w = await make(env_port=8181)
    await w.login()
    body = await (await w.call("GET", "/api/settings")).json()
    assert body["settings"]["http_port"] == 8181 and body["settings"]["http_port_from_env"] is True
    assert body["restart_needed"] is False
    before, settings = w.store.path.read_bytes(), w.hub.settings
    for port in (8282, 8181, 8080):
        resp = await w.call("PUT", "/api/settings", {"http_port": port, "openai": {"voice": "cedar"}})
        body = await resp.json()
        assert resp.status == 409 and body["error"] == "port_from_env" and "VOICE_HUB_PORT" in body["message"]
    assert w.store.path.read_bytes() == before and w.hub.settings == settings
    resp = await w.call("PUT", "/api/settings", {"advertise_host": "192.0.2.5"})
    body = await resp.json()
    assert resp.status == 200 and body["restart_needed"] is True
    assert body["settings"]["http_port"] == 8181 and body["settings"]["http_port_from_env"] is True
    assert w.store.load().http_port == 8080


@pytest.mark.parametrize("patch, status, code, field", [
    ({"http_port": 80}, 400, "invalid", "http_port"),
    ({"http_port": 1023}, 400, "invalid", "http_port"),
    ({"advertise_host": "http://192.0.2.5:8080"}, 400, "invalid", "advertise_host"),
    ({"advertise_host": "192.0.2.5:8080"}, 400, "invalid", "advertise_host"),
    ({"openai": {"vad_threshold": 5}}, 400, "invalid", "openai.vad_threshold"),
    ({"openai": {"api_key": "  ", "model": "gpt-x"}}, 400, "invalid", "openai.api_key"),
    ({"timing": {"follow_up_s": 0}}, 400, "invalid", "timing.follow_up_s"),
    ({"assistant": {"language": "fr"}}, 400, "invalid", "assistant.language"),
    ({"password_hash": "scrypt$1$1$1$AA$AA"}, 400, "invalid", "password_hash"),
    ({"devices": []}, 400, "invalid", "devices"),
    ({"devices": [{"name": "kitchen", "host": "192.0.2.50"}]}, 400, "invalid", "devices"),
    ({"version": 2}, 400, "invalid", "version"),
    ({"http_port": 0}, 400, "invalid", "http_port"),
    ([], 400, "bad_json", None),
    ("nope", 400, "bad_json", None),
])
async def test_put_settings_refuses_bad_changes(make, patch, status, code, field):
    w = await make()
    await w.login()
    before, settings = w.store.path.read_bytes(), w.hub.settings
    resp = await w.call("PUT", "/api/settings", patch)
    body = await resp.json()
    assert resp.status == status and body["error"] == code
    if field is not None:
        assert body["message"].startswith(field)
    assert w.store.path.read_bytes() == before and w.hub.settings == settings


async def test_error_bodies_escape_what_the_page_sent(make):
    w = await make()
    await w.login()
    resp = await w.call("PUT", "/api/settings", {"openai": {"ключ": 1}})
    text = await resp.text()
    assert resp.status == 400 and "ключ" not in text and "\\u043a" in text
    assert (await resp.json())["message"] == "openai.ключ: unknown setting"


async def test_a_failed_save_is_reported(make, monkeypatch):
    w = await make()
    await w.login()
    settings = w.hub.settings

    def full(_settings):
        raise SettingsError(f"{w.store.path}: cannot be saved (No space left on device)")

    monkeypatch.setattr(w.store, "save", full)
    resp = await w.call("PUT", "/api/settings", {"openai": {"model": "gpt-x"}})
    assert resp.status == 500
    assert await resp.json() == {"error": "save_failed",
                                 "message": f"{w.store.path}: cannot be saved (No space left on device)"}
    assert w.hub.settings == settings


async def test_devices_show_status_and_wake_words(make):
    w = await make()
    await w.login()
    resp = await w.call("GET", "/api/devices")
    words = {"available": [{"id": "okay_nabu", "phrase": "Okay Nabu"}, {"id": "hey_mycroft", "phrase": "Hey Mycroft"}],
             "active": ["okay_nabu"]}
    online = {"port": 6053, "key_set": True, "connected": True, "wake_words": words, "volume": 50}
    assert await resp.json() == {"devices": [{"name": "kitchen", "host": "192.0.2.10"} | online,
                                             {"name": "hall", "host": "192.0.2.11"} | online]}
    w.links.made["hall"].connected = False
    w.links.made["hall"].wake_words = WakeWords()
    w.links.made["kitchen"].level = 0.78
    kitchen, hall = (await (await w.call("GET", "/api/devices")).json())["devices"]
    assert hall["connected"] is False and hall["wake_words"] == {"available": [], "active": []}
    assert kitchen["volume"] == 78 and hall["volume"] is None
    w.links.made["kitchen"].level = None
    assert (await (await w.call("GET", "/api/devices")).json())["devices"][0]["volume"] is None


async def test_adopt_gives_the_speaker_a_key_and_returns_its_name(make):
    w = await make()
    await w.login()
    resp = await w.call("POST", "/api/devices/adopt", {"host": "192.0.2.30"})
    assert resp.status == 200
    body = await resp.json()
    assert body["name"] == "speaker-a1b2c3"
    assert [d["name"] for d in body["devices"]] == ["kitchen", "hall", "speaker-a1b2c3"]
    assert body["devices"][2]["key_set"] is True and body["devices"][2]["connected"] is True
    assert w.speakers.args[0] == ("192.0.2.30", 6053, ZERO_NOISE_PSK)
    new_key = w.speakers.client.keys[0].decode("ascii")
    assert w.store.load().devices[2] == DeviceSettings(name="speaker-a1b2c3", host="192.0.2.30", key=new_key)
    assert new_key not in await resp.text()


async def test_adopt_uses_the_given_port(make):
    w = await make()
    await w.login()
    assert (await w.call("POST", "/api/devices/adopt", {"host": "192.0.2.30", "port": 6055})).status == 200
    assert w.speakers.args[0] == ("192.0.2.30", 6055, ZERO_NOISE_PSK) and w.store.load().devices[2].port == 6055


def has_key() -> AdoptClient:
    return AdoptClient(DeviceInfo(name="speaker-a1b2c3", api_encryption_provisionable=False))


def failing(error: Exception, info: DeviceInfo | None = None) -> AdoptClient:
    client = AdoptClient(info)
    client.fail_connect = error
    return client


def wrong_key() -> AdoptClient:
    return failing(InvalidEncryptionKeyAPIError("Invalid encryption key", "speaker-d4e5f6"))


def keyless() -> AdoptClient:
    return AdoptClient(DeviceInfo(name="speaker-d4e5f6", api_encryption_provisionable=True))


def refusing() -> AdoptClient:
    client = AdoptClient()
    client.accept = False
    return client


@pytest.mark.parametrize("speaker, status, code", [
    (has_key, 409, "has_key"),
    (lambda: failing(EncryptionPlaintextAPIError("plaintext")), 409, "unsupported"),
    (lambda: failing(APIConnectionError("no route to host")), 502, "unreachable"),
    (lambda: AdoptClient(DeviceInfo(name=" ", api_encryption_provisionable=True)), 502, "no_name"),
    (refusing, 502, "refused"),
    (lambda: AdoptClient(DeviceInfo(name="kitchen", api_encryption_provisionable=True)), 409, "name_taken"),
])
async def test_adopt_explains_failures(make, speaker, status, code):
    w = await make()
    await w.login()
    before = w.store.path.read_bytes()
    w.speakers.client = speaker()
    resp = await w.call("POST", "/api/devices/adopt", {"host": "192.0.2.30"})
    body = await resp.json()
    assert resp.status == status and body["error"] == code and body["message"]
    assert w.store.path.read_bytes() == before


@pytest.mark.parametrize("body", [{"host": ""}, {}, {"host": None}, {"host": "192.0.2.30", "port": "6053"},
                                  {"host": "192.0.2.30", "port": 0}, {"host": "192.0.2.30", "key": key(7)}])
async def test_adopt_checks_its_input_before_connecting(make, body):
    w = await make()
    await w.login()
    resp = await w.call("POST", "/api/devices/adopt", body)
    assert resp.status == 400 and (await resp.json())["error"] == "invalid"
    assert w.speakers.args == []


@pytest.mark.parametrize("method, path, extra", [
    ("POST", "/api/devices/adopt", {}), ("POST", "/api/devices", {}), ("POST", "/api/devices", {"key": key(7)}),
    ("PUT", "/api/devices/kitchen", {}),
])
@pytest.mark.parametrize("host", ["http://192.0.2.30", "192.0.2.30:6053", "speaker.local/", "my speaker"])
async def test_speaker_addresses_must_be_a_host_without_scheme_port_or_path(make, method, path, extra, host):
    w = await make()
    await w.login()
    before = w.store.path.read_bytes()
    resp = await w.call(method, path, {"host": host} | extra)
    body = await resp.json()
    assert resp.status == 400 and body["error"] == "invalid"
    assert body["message"] == "host: must be a host name or an IP address, without http://, a port or a path"
    assert w.speakers.args == [] and w.store.path.read_bytes() == before


async def test_a_speaker_may_be_adopted_by_a_bare_ipv6_address(make):
    w = await make()
    await w.login()
    resp = await w.call("POST", "/api/devices/adopt", {"host": "2001:db8::30"})
    assert resp.status == 200
    assert w.speakers.args[0] == ("2001:db8::30", 6053, ZERO_NOISE_PSK)


async def test_adopt_save_failure_is_reported(make, monkeypatch):
    w = await make()
    await w.login()

    def full(_settings):
        raise SettingsError("settings.json: cannot be saved (No space left on device)")

    monkeypatch.setattr(w.store, "save", full)
    resp = await w.call("POST", "/api/devices/adopt", {"host": "192.0.2.30"})
    assert resp.status == 500 and (await resp.json())["error"] == "save_failed"
    assert w.speakers.client.keys == []


async def test_slow_adoption_keeps_the_request_open(make):
    w = await make()
    await w.login()
    w.speakers.client = GatedClient()
    request = asyncio.create_task(w.call("POST", "/api/devices/adopt", {"host": "192.0.2.30"}))
    await asyncio.sleep(0.3)
    assert not request.done()
    w.speakers.client.gate.set()
    resp = await request
    assert resp.status == 200 and (await resp.json())["name"] == "speaker-a1b2c3"


async def test_adoption_finishes_when_the_page_stops_waiting(make):
    w = await make()
    await w.login()
    w.speakers.client = GatedClient()
    with pytest.raises(asyncio.TimeoutError):
        await w.call("POST", "/api/devices/adopt", {"host": "192.0.2.30"}, timeout=aiohttp.ClientTimeout(total=0.2))
    w.speakers.client.gate.set()
    for _ in range(100):
        if w.hub.registry.link("speaker-a1b2c3") is not None:
            break
        await asyncio.sleep(0.02)
    assert w.store.load().devices[2].name == "speaker-a1b2c3" and w.hub.registry.link("speaker-a1b2c3").connected


@pytest.mark.parametrize("body", [{"host": "192.0.2.30"}, {"host": "192.0.2.30", "key": None},
                                  {"host": "192.0.2.30", "key": "  "}])
async def test_adding_without_a_key_adopts(make, body):
    w = await make()
    await w.login()
    resp = await w.call("POST", "/api/devices", body)
    assert resp.status == 200 and (await resp.json())["name"] == "speaker-a1b2c3"
    assert w.speakers.args[0] == ("192.0.2.30", 6053, ZERO_NOISE_PSK) and len(w.speakers.client.keys) == 1


async def test_adding_with_a_key_takes_the_speakers_own_name(make):
    w = await make()
    await w.login()
    w.speakers.client = AdoptClient(DeviceInfo(name="speaker-d4e5f6", api_encryption_provisionable=False))
    resp = await w.call("POST", "/api/devices", {"host": "192.0.2.31", "port": 6054, "key": key(7)})
    assert resp.status == 200
    body = await resp.json()
    assert body["name"] == "speaker-d4e5f6" and [d["name"] for d in body["devices"]][-1] == "speaker-d4e5f6"
    assert w.speakers.args == [("192.0.2.31", 6054, key(7))] and w.speakers.client.keys == []
    assert w.store.load().devices[2] == DeviceSettings(name="speaker-d4e5f6", host="192.0.2.31", port=6054, key=key(7))
    assert w.hub.registry.link("speaker-d4e5f6").connected
    assert key(7) not in await resp.text()


@pytest.mark.parametrize("speaker, body, status, code", [
    (lambda: failing(InvalidEncryptionKeyAPIError("Invalid encryption key", "speaker-d4e5f6")),
     {"host": "192.0.2.31", "key": key(7)}, 409, "wrong_key"),
    (lambda: failing(APIConnectionError("no route to host")), {"host": "192.0.2.31", "key": key(7)}, 502,
     "unreachable"),
    (lambda: AdoptClient(DeviceInfo(name="kitchen")), {"host": "192.0.2.31", "key": key(7)}, 409, "name_taken"),
    (AdoptClient, {"host": "192.0.2.31", "key": "c2hvcnQ="}, 400, "invalid"),
    (AdoptClient, {"host": "192.0.2.31", "key": 7}, 400, "invalid"),
    (AdoptClient, {"host": "192.0.2.31", "name": "kitchen-2"}, 400, "invalid"),
    (AdoptClient, {"key": key(7)}, 400, "invalid"),
])
async def test_adding_explains_failures(make, speaker, body, status, code):
    w = await make()
    await w.login()
    before = w.store.path.read_bytes()
    w.speakers.client = speaker()
    resp = await w.call("POST", "/api/devices", body)
    assert resp.status == status and (await resp.json())["error"] == code
    assert w.store.path.read_bytes() == before and w.speakers.client.keys == []
    if status == 400:
        assert w.speakers.args == []


async def test_adding_a_keyless_speaker_with_a_key_says_to_leave_the_key_out(make):
    w = await make()
    await w.login()
    before = w.store.path.read_bytes()
    w.speakers.client, w.speakers.zero = wrong_key(), keyless()
    resp = await w.call("POST", "/api/devices", {"host": "192.0.2.31", "key": key(7)})
    body = await resp.json()
    assert resp.status == 409 and body["error"] == "no_key_yet" and "without a key" in body["message"]
    assert w.speakers.args == [("192.0.2.31", 6053, key(7)), ("192.0.2.31", 6053, ZERO_NOISE_PSK)]
    assert w.speakers.zero.keys == [] and w.store.path.read_bytes() == before


async def test_change_a_speakers_address_and_keep_its_key(make):
    w = await make()
    await w.login()
    old = w.links.made["kitchen"]
    resp = await w.call("PUT", "/api/devices/kitchen", {"host": "192.0.2.40", "port": 6060})
    assert resp.status == 200
    kitchen = (await resp.json())["devices"][0]
    assert kitchen["name"] == "kitchen" and kitchen["host"] == "192.0.2.40" and kitchen["port"] == 6060
    assert kitchen["key_set"] is True and kitchen["connected"] is True
    assert key(1) not in await resp.text()
    moved = DeviceSettings(name="kitchen", host="192.0.2.40", port=6060, key=key(1))
    assert w.store.load().devices == (moved, HALL) and w.hub.settings.devices == (moved, HALL)
    assert ("stop", "kitchen") in w.links.journal and ("stop", "hall") not in w.links.journal
    link = w.hub.registry.link("kitchen")
    assert link is not old and link.device == moved and link.connected


async def test_address_change_without_a_port_keeps_the_port(make):
    w = await make(standard(devices=(DeviceSettings(name="kitchen", host="192.0.2.10", port=6054, key=key(1)),)))
    await w.login()
    resp = await w.call("PUT", "/api/devices/kitchen", {"host": " 192.0.2.40 "})
    assert resp.status == 200
    assert w.store.load().devices[0] == DeviceSettings(name="kitchen", host="192.0.2.40", port=6054, key=key(1))


@pytest.mark.parametrize("name, body, status, code", [
    ("kitchen", {"host": ""}, 400, "invalid"),
    ("kitchen", {}, 400, "invalid"),
    ("kitchen", {"host": "192.0.2.40", "port": 0}, 400, "invalid"),
    ("kitchen", {"host": "192.0.2.40", "port": "6053"}, 400, "invalid"),
    ("kitchen", {"host": "192.0.2.40", "key": key(7)}, 400, "invalid"),
    ("kitchen", {"host": "192.0.2.40", "name": "hall"}, 400, "invalid"),
    ("kitchen", "[]", 400, "bad_json"),
    ("nobody", {"host": "192.0.2.40"}, 404, "unknown_speaker"),
])
async def test_address_change_refuses_bad_input(make, name, body, status, code):
    w = await make()
    await w.login()
    before, settings = w.store.path.read_bytes(), w.hub.settings
    resp = await w.call("PUT", f"/api/devices/{name}", body)
    assert resp.status == status and (await resp.json())["error"] == code
    assert w.store.path.read_bytes() == before and w.hub.settings == settings
    assert w.links.journal == [("start", "kitchen"), ("start", "hall")]


async def test_remove_a_speaker(make):
    w = await make()
    await w.login()
    resp = await w.call("DELETE", "/api/devices/hall")
    assert resp.status == 200 and [d["name"] for d in (await resp.json())["devices"]] == ["kitchen"]
    assert w.store.load().devices == (KITCHEN,) and ("stop", "hall") in w.links.journal
    assert w.hub.registry.link("hall") is None


async def test_removing_an_unknown_speaker(make):
    w = await make()
    await w.login()
    before = w.store.path.read_bytes()
    resp = await w.call("DELETE", "/api/devices/nobody")
    assert resp.status == 404 and (await resp.json())["error"] == "unknown_speaker"
    assert w.store.path.read_bytes() == before


async def test_choose_a_wake_word(make):
    w = await make()
    await w.login()
    resp = await w.call("PUT", "/api/devices/kitchen/wake-word", {"wake_word": "hey_mycroft"})
    assert resp.status == 200
    kitchen = (await resp.json())["devices"][0]
    assert kitchen["name"] == "kitchen" and kitchen["wake_words"]["active"] == ["hey_mycroft"]
    assert w.links.made["kitchen"].chosen == ["hey_mycroft"]


@pytest.mark.parametrize("name, body, fail, status, code", [
    ("nobody", {"wake_word": "hey_mycroft"}, None, 404, "unknown_speaker"),
    ("kitchen", {"wake_word": "nope"}, None, 409, "unknown"),
    ("kitchen", {"wake_word": "hey_mycroft"}, WakeWordError("kitchen is not connected", "offline"), 409, "offline"),
    ("kitchen", {"wake_word": "hey_mycroft"}, WakeWordError("could not change", "failed"), 502, "failed"),
    ("kitchen", {"wake_word": ""}, None, 400, "invalid"),
    ("kitchen", {"wake_word": 5}, None, 400, "invalid"),
    ("kitchen", {}, None, 400, "invalid"),
    ("kitchen", {"wake_word": "hey_mycroft", "also": 1}, None, 400, "invalid"),
])
async def test_wake_word_failures(make, name, body, fail, status, code):
    w = await make()
    await w.login()
    w.links.made["kitchen"].fail = fail
    resp = await w.call("PUT", f"/api/devices/{name}/wake-word", body)
    assert resp.status == status and (await resp.json())["error"] == code
    assert w.links.made["kitchen"].chosen == []


@pytest.mark.parametrize("sent, level", [(35, 0.35), (0, 0.0), (100, 1.0), (40.0, 0.4)])
async def test_set_a_speakers_volume(make, sent, level):
    w = await make()
    await w.login()
    resp = await w.call("PUT", "/api/devices/kitchen/volume", {"volume": sent})
    assert resp.status == 200
    kitchen, hall = (await resp.json())["devices"]
    assert kitchen["name"] == "kitchen" and kitchen["volume"] == round(sent) and hall["volume"] == 50
    assert w.links.made["kitchen"].levels == [level] and w.links.made["hall"].levels == []
    assert (await (await w.call("GET", "/api/devices")).json())["devices"][0]["volume"] == round(sent)


@pytest.mark.parametrize("body", [
    {"volume": True}, {"volume": False}, {"volume": 50.5}, {"volume": "50"}, {"volume": None}, {"volume": -1},
    {"volume": 101}, {"volume": 1e300}, {"volume": []}, {"volume": {}}, {}, {"volume": 50, "also": 1},
    '{"volume": NaN}', '{"volume": Infinity}', '{"volume": -Infinity}', '{"volume": 1e400}',
])
async def test_volume_must_be_a_whole_percent(make, body):
    w = await make()
    await w.login()
    resp = await w.call("PUT", "/api/devices/kitchen/volume", body)
    assert resp.status == 400 and (await resp.json())["error"] == "invalid"
    assert w.links.made["kitchen"].levels == []


async def test_a_refused_volume_names_the_field_and_the_range(make):
    w = await make()
    await w.login()
    resp = await w.call("PUT", "/api/devices/kitchen/volume", {"volume": 101})
    assert await resp.json() == {"error": "invalid", "message": "volume: must be a whole number from 0 to 100"}


@pytest.mark.parametrize("name, fail, offline, status, code", [
    ("nobody", None, False, 404, "unknown_speaker"),
    ("kitchen", None, True, 409, "offline"),
    ("kitchen", VolumeError("kitchen is not connected or has no media player", "offline"), False, 409, "offline"),
    ("kitchen", VolumeError("could not set the volume on kitchen", "failed"), False, 502, "failed"),
])
async def test_volume_failures(make, name, fail, offline, status, code):
    w = await make()
    await w.login()
    w.links.made["kitchen"].volume_fail = fail
    w.links.made["kitchen"].connected = not offline
    resp = await w.call("PUT", f"/api/devices/{name}/volume", {"volume": 40})
    assert resp.status == status and (await resp.json())["error"] == code
    assert w.links.made["kitchen"].levels == []


async def test_a_speaker_without_a_key_has_no_volume_to_set(make):
    porch = DeviceSettings(name="porch", host="192.0.2.12")
    w = await make(standard(devices=(KITCHEN, porch)))
    await w.login()
    assert (await (await w.call("GET", "/api/devices")).json())["devices"][1]["volume"] is None
    resp = await w.call("PUT", "/api/devices/porch/volume", {"volume": 40})
    assert resp.status == 404 and (await resp.json())["error"] == "unknown_speaker"


async def test_discover_lists_speakers_found_in_the_network(make):
    w = await make()
    await w.login()
    resp = await w.call("GET", "/api/discover")
    assert await resp.json() == {"found": [
        {"name": "kitchen", "host": "192.0.2.10", "port": 6053, "provisionable": False, "friendly_name": "Kitchen",
         "added": True},
        {"name": "speaker-a1b2c3", "host": "192.0.2.30", "port": 6053, "provisionable": True,
         "friendly_name": "Speaker", "added": False},
    ]}


async def test_discover_stops_offering_a_speaker_that_is_already_adopted_and_online(make):
    stale = Found(name="speaker-a1b2c3", host="192.0.2.30", port=6053, provisionable=True, friendly_name="Speaker")
    w = await make(found=[stale])
    await w.login()
    assert (await w.call("POST", "/api/devices/adopt", {"host": "192.0.2.30"})).status == 200
    (found,) = (await (await w.call("GET", "/api/discover")).json())["found"]
    assert found["name"] == "speaker-a1b2c3" and found["added"] is True and found["provisionable"] is False

    w.links.made["speaker-a1b2c3"].connected = False
    (found,) = (await (await w.call("GET", "/api/discover")).json())["found"]
    assert found["added"] is True and found["provisionable"] is True


async def test_discover_still_offers_a_reflashed_speaker_whose_saved_entry_is_offline(make):
    w = await make(found=[Found(name="kitchen", host="192.0.2.10", port=6053, provisionable=True,
                                friendly_name="Kitchen")])
    await w.login()
    w.links.made["kitchen"].connected = False
    (found,) = (await (await w.call("GET", "/api/discover")).json())["found"]
    assert found["added"] is True and found["provisionable"] is True


SAFE_BODIES = [None, "", "not json", "[]", {}, {"password": PASSWORD}]
SWEEP_BODIES = SAFE_BODIES + [{"current": PASSWORD, "new": NEW_PASSWORD}]
EXAMPLES = {
    ("POST", "/api/setup"): [{"password": NEW_PASSWORD}, {"password": SHORT_PASSWORD}],
    ("POST", "/api/login"): [{"password": WRONG_GUESS}, {"password": PASSWORD}],
    ("PUT", "/api/settings"): [
        {"openai": {"api_key": NEW_OPENAI_KEY, "vad_threshold": 7}}, {"openai": {"api_key": NEW_OPENAI_KEY}},
        {"openai": {"api_key": NEW_OPENAI_KEY, "model": " "}}, {"devices": [{"name": "kitchen", "key": key(7)}]},
        {"password_hash": FAKE_HASH}, {"timing": {"follow_up_s": -1}}, {"http_port": 8181},
    ],
    ("POST", "/api/devices"): [{"host": "192.0.2.31", "key": key(7)}, {"host": "192.0.2.31", "key": key(7)[:-2]},
                               {"host": "192.0.2.30"}, {"host": "192.0.2.31", "key": key(7), "name": "x"}],
    ("POST", "/api/devices/adopt"): [{"host": "192.0.2.30"}, {"host": ""}, {"host": "192.0.2.30"}],
    ("PUT", "/api/devices/{name}/wake-word"): [{"wake_word": "hey_mycroft"}, {"wake_word": "nope"}],
    ("PUT", "/api/devices/{name}/volume"): [{"volume": 40}, {"volume": 101}, {"volume": True}, '{"volume": NaN}'],
    ("PUT", "/api/devices/{name}"): [{"host": "192.0.2.40"}, {"host": ""}, {"host": "192.0.2.41", "key": key(7)},
                                     {"host": "192.0.2.42", "port": 6060}],
    ("POST", "/api/password"): [{"current": WRONG_GUESS, "new": NEW_PASSWORD},
                                {"current": PASSWORD, "new": SHORT_PASSWORD}],
}
FOREIGN = ({"Origin": "http://evil.test"}, {"Host": "evil.test"}, {"Host": "evil.test", "Origin": "http://evil.test"})
SPEAKERS = [
    lambda: AdoptClient(),
    has_key,
    lambda: failing(APIConnectionError("no route to host")),
    lambda: failing(InvalidEncryptionKeyAPIError("Invalid encryption key", "speaker-d4e5f6")),
    lambda: AdoptClient(DeviceInfo(name="speaker-d4e5f6", api_encryption_provisionable=False)),
    lambda: AdoptClient(DeviceInfo(name="kitchen", api_encryption_provisionable=True)),
    refusing,
]


async def test_no_response_reveals_a_key_or_the_password_hash(make, caplog):
    caplog.set_level(logging.DEBUG)
    w = await make(delay=0.0)
    stored_hash = w.hub.settings.password_hash
    seen: list[str] = []
    statuses: set[int] = set()

    async def hit(method, path, body=None, headers=None):
        resp = await w.call(method, path, body, headers=headers)
        text = await resp.text()
        statuses.add(resp.status)
        seen.append(f"{resp.status} {list(resp.headers.items())} {text}")
        try:
            seen.append(json.dumps(json.loads(text), ensure_ascii=False))
        except ValueError:
            pass

    routes = sorted(api_routes(w.app)) + [("GET", "/api/nope"), ("PATCH", "/api/settings"),
                                          ("OPTIONS", "/api/settings")]
    for method, path in routes:
        for body in SWEEP_BODIES:
            for headers in (None, *FOREIGN, {"Content-Type": "text/plain"}):
                await hit(method, concrete(path), body, headers)
                w.forget_cookies()

    await w.login()
    for speaker in SPEAKERS:
        w.speakers.client = speaker()
        for method, path in routes:
            for body in SAFE_BODIES + EXAMPLES.get((method, path), []):
                await hit(method, concrete(path), body)
                if path == "/api/logout":
                    await w.login()
            for headers in FOREIGN:
                await hit(method, concrete(path), {}, headers)
            await hit(method, concrete(path), "{}", {"Content-Type": "text/plain"})

    await hit("PUT", "/api/devices/hall", {"host": "192.0.2.44"})
    w.speakers.client, w.speakers.zero = wrong_key(), keyless()
    await hit("POST", "/api/devices", {"host": "192.0.2.45", "key": key(9)})
    w.speakers.zero = None
    for fail in ("offline", "unknown", "failed"):
        link = w.links.made.get("hall")
        if link is not None:
            link.fail = WakeWordError(f"hall: {fail}", fail)
        await hit("PUT", "/api/devices/hall/wake-word", {"wake_word": "hey_mycroft"})
    for fail in ("offline", "failed"):
        link = w.links.made.get("hall")
        if link is not None:
            link.volume_fail = VolumeError(f"hall: {fail}", fail)
        await hit("PUT", "/api/devices/hall/volume", {"volume": 40})

    def full(_settings):
        raise SettingsError(f"{w.store.path}: cannot be saved (No space left on device)")

    w.store.save = full
    w.speakers.client = AdoptClient(DeviceInfo(name="speaker-0d0e0f", api_encryption_provisionable=True))
    await hit("PUT", "/api/settings", {"openai": {"api_key": NEW_OPENAI_KEY}})
    await hit("POST", "/api/devices/adopt", {"host": "192.0.2.33"})
    w.speakers.client = AdoptClient(DeviceInfo(name="speaker-0a0b0c"))
    await hit("POST", "/api/devices", {"host": "192.0.2.32", "key": key(8)})
    await hit("POST", "/api/password", {"current": PASSWORD, "new": NEW_PASSWORD})
    del w.store.save
    await hit("POST", "/api/password", {"current": PASSWORD, "new": NEW_PASSWORD})
    await hit("GET", "/api/settings")
    await hit("POST", "/api/logout")

    final = w.store.load()
    secrets = {OPENAI_KEY, NEW_OPENAI_KEY, key(1), key(2), key(7), key(8), key(9), key(7)[:-2], PASSWORD,
               NEW_PASSWORD, WRONG_GUESS, SHORT_PASSWORD, FAKE_HASH, stored_hash, *stored_hash.split("$")[-2:],
               final.password_hash, *final.password_hash.split("$")[-2:]}
    secrets |= {d.key for d in final.devices if d.key is not None}
    assert final.password_hash != stored_hash and len(final.devices) > 2
    assert {200, 400, 401, 403, 404, 405, 409, 415, 500, 502} <= statuses
    assert len(seen) > 500
    assert any('"foreign_host"' in text for text in seen) and any('"no_key_yet"' in text for text in seen)
    assert ("stop", "hall") in w.links.journal and final.devices[0].name == "hall"
    for secret in secrets:
        assert not any(secret in text for text in seen), secret
        assert secret not in caplog.text, secret
