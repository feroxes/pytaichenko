import asyncio
import base64
import logging
import os
import signal
import socket
import stat
import wave
from dataclasses import replace

import aiohttp
import pytest
from aiohttp.test_utils import unused_port

from voice_hub import app, registry
from voice_hub.app import StartError, build_app, main, run
from voice_hub.audio import DEVICE_RATE, wav_header
from voice_hub.config import ConfigError, Timing
from voice_hub.realtime import RealtimeSession
from voice_hub.registry import Hub
from voice_hub.settings import AssistantSettings, DeviceSettings, OpenAISettings, Settings, SettingsStore, openai_config
from voice_hub.streams import AudioStreams, local_ip


class RecordingLink:
    created: list = []

    def __init__(self, cfg, make_conversation) -> None:
        self.cfg = cfg
        self.name = cfg.name
        self.connected = False
        self.volume = 0.7
        self.trace: list[str] = []
        self.conversation = make_conversation(self)
        RecordingLink.created.append(self)

    async def start(self) -> None:
        self.trace.append("start")

    async def stop(self) -> None:
        self.trace.append("stop")


class RecordingConversation:
    def __init__(self, **kwargs) -> None:
        self.kwargs = kwargs


class FakeDiscovery:
    made: list = []

    def __init__(self) -> None:
        self.trace: list[str] = []
        FakeDiscovery.made.append(self)

    async def start(self) -> None:
        self.trace.append("start")

    async def stop(self) -> None:
        self.trace.append("stop")

    def snapshot(self) -> list:
        return []


def write_wav(path, rate: int, frames: int) -> None:
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b"\x01\x00" * frames)


def device_cfg(name="kitchen", **kw) -> DeviceSettings:
    if "debug_input_wav" in kw:
        kw["debug_input_wav"] = str(kw["debug_input_wav"])
    return DeviceSettings(name=name, host="192.0.2.10", key=device_key(), **kw)


def config(*devices, **kw) -> Settings:
    return Settings(devices=tuple(devices), openai=OpenAISettings(api_key="sk-test"), advertise_host="hub.test", **kw)


async def serve(cfg: Settings, store: SettingsStore, password: str | None = None,
                hosts: frozenset[str] = frozenset(), env_port: int | None = None) -> asyncio.Task:
    task = asyncio.create_task(run(store, cfg, password, hosts, env_port))
    async with aiohttp.ClientSession() as http:
        for _ in range(200):
            if task.done():
                break
            try:
                async with http.get(f"http://127.0.0.1:{env_port or cfg.http_port}/health"):
                    return task
            except aiohttp.ClientConnectionError:
                await asyncio.sleep(0.02)
    await task
    raise AssertionError("the service did not start")


async def terminate(task: asyncio.Task) -> None:
    os.kill(os.getpid(), signal.SIGTERM)
    await asyncio.wait_for(task, 5.0)


@pytest.fixture(autouse=True)
def reset_links(monkeypatch):
    RecordingLink.created = []
    FakeDiscovery.made = []
    monkeypatch.setattr(app, "Discovery", FakeDiscovery)


@pytest.fixture
def store(tmp_path) -> SettingsStore:
    return SettingsStore(tmp_path / "data" / "settings.json")


@pytest.fixture
def fake_devices(monkeypatch):
    monkeypatch.setattr(registry, "DeviceLink", RecordingLink)
    monkeypatch.setattr(registry, "Conversation", RecordingConversation)


async def test_health_200_when_all_connected(aiohttp_client):
    client = await aiohttp_client(build_app(AudioStreams(), lambda: {"kitchen": True}))
    resp = await client.get("/health")
    assert resp.status == 200
    assert await resp.json() == {"devices": {"kitchen": True}}


async def test_health_503_when_device_offline(aiohttp_client):
    client = await aiohttp_client(build_app(AudioStreams(), lambda: {"kitchen": False}))
    resp = await client.get("/health")
    assert resp.status == 503
    assert await resp.json() == {"devices": {"kitchen": False}}


async def test_health_503_when_any_device_offline(aiohttp_client):
    client = await aiohttp_client(build_app(AudioStreams(), lambda: {"kitchen": True, "hall": False}))
    resp = await client.get("/health")
    assert resp.status == 503
    assert await resp.json() == {"devices": {"kitchen": True, "hall": False}}


async def test_health_reflects_live_state(aiohttp_client):
    state = {"kitchen": False}
    client = await aiohttp_client(build_app(AudioStreams(), lambda: state))
    assert (await client.get("/health")).status == 503
    state["kitchen"] = True
    assert (await client.get("/health")).status == 200


async def test_health_200_without_devices(aiohttp_client):
    client = await aiohttp_client(build_app(AudioStreams(), dict))
    resp = await client.get("/health")
    assert resp.status == 200
    assert await resp.json() == {"devices": {}}


async def test_audio_route_serves_stream(aiohttp_client):
    streams = AudioStreams()
    client = await aiohttp_client(build_app(streams, dict))
    stream = streams.create()
    stream.write(b"\x01\x02")
    stream.finish()
    resp = await client.get(f"/audio/{stream.id}.wav")
    assert resp.status == 200 and resp.headers["Content-Type"] == "audio/wav"
    assert await resp.read() == wav_header(DEVICE_RATE) + b"\x01\x02"


async def test_audio_route_404_for_unknown_stream(aiohttp_client):
    client = await aiohttp_client(build_app(AudioStreams(), dict))
    assert (await client.get("/audio/nope.wav")).status == 404


def test_app_uses_local_ip_from_streams():
    assert app.local_ip is local_ip


def device_key(n: int = 1) -> str:
    return base64.b64encode(bytes([n]) * 32).decode("ascii")


def write_settings(data_dir, settings: Settings) -> None:
    SettingsStore(data_dir / "settings.json").save(settings)


@pytest.fixture
def passwords() -> list:
    return []


@pytest.fixture
def ports() -> list:
    return []


@pytest.fixture
def recorded_runs(monkeypatch, passwords, ports):
    seen: list[Settings] = []

    async def fake_run(store: SettingsStore, settings: Settings, password: str | None = None,
                       hosts: frozenset[str] = frozenset(), env_port: int | None = None) -> None:
        assert store.load(env_port) == settings
        seen.append(settings)
        passwords.append((password, hosts))
        ports.append(env_port)

    monkeypatch.setattr(app, "run", fake_run)
    return seen


def test_main_reports_config_error(tmp_path, capsys):
    path = tmp_path / "config.yaml"
    path.write_text("devices: []\nopenai:\n  modle: x\n")
    assert main(["--data", str(tmp_path / "data"), "--config", str(path)], env={}) == 2
    captured = capsys.readouterr()
    assert "modle" in captured.err and captured.out == ""
    assert not (tmp_path / "data" / "settings.json").exists()


def test_main_reports_missing_config_file(tmp_path, capsys):
    assert main(["--data", str(tmp_path), "--config", str(tmp_path / "missing.yaml")], env={}) == 2
    assert "missing.yaml" in capsys.readouterr().err


def test_main_reports_missing_device_key_variable(tmp_path, capsys):
    path = tmp_path / "config.yaml"
    path.write_text("devices:\n  - name: kitchen\n    host: 192.0.2.10\n    encryption_key_env: KEY\n")
    assert main(["--data", str(tmp_path), "--config", str(path)], env={"OPENAI_API_KEY": "sk-test"}) == 2
    assert "KEY" in capsys.readouterr().err
    assert not (tmp_path / "settings.json").exists()


def test_main_reports_unreadable_debug_wav(tmp_path, capsys):
    path = tmp_path / "config.yaml"
    path.write_text(
        f"http_port: {unused_port()}\nadvertise_host: hub.test\n"
        f"devices:\n  - name: kitchen\n    host: 192.0.2.10\n    encryption_key_env: KEY\n"
        f"    debug_input_wav: {tmp_path / 'missing.wav'}\n"
    )
    env = {"OPENAI_API_KEY": "sk-test", "KEY": device_key()}
    assert main(["--data", str(tmp_path / "data"), "--config", str(path)], env=env) == 2
    assert "missing.wav" in capsys.readouterr().err


def test_main_seeds_settings_from_the_config(tmp_path, recorded_runs):
    path = tmp_path / "config.yaml"
    path.write_text(
        "http_port: 9191\ndevices:\n  - name: kitchen\n    host: 192.0.2.10\n    encryption_key_env: KEY\n"
        "openai:\n  transcription_model: gpt-4o-mini-transcribe\n"
    )
    data = tmp_path / "data"
    env = {"OPENAI_API_KEY": "sk-test", "KEY": device_key()}
    assert main(["--data", str(data), "--config", str(path)], env=env) == 0
    saved = SettingsStore(data / "settings.json").load()
    assert saved is not None and saved.http_port == 9191 and saved.openai.api_key == "sk-test"
    assert saved.devices[0].key == device_key() and saved.openai.log_heard is True
    assert stat.S_IMODE((data / "settings.json").stat().st_mode) == 0o600
    (cfg,) = recorded_runs
    assert cfg == saved
    assert openai_config(cfg).transcription_model == "gpt-4o-mini-transcribe" and openai_config(cfg).language == "uk"


def test_main_seeds_defaults_without_a_config(tmp_path, recorded_runs):
    assert main(["--data", str(tmp_path)], env={"OPENAI_API_KEY": "sk-test"}) == 0
    assert SettingsStore(tmp_path / "settings.json").load() == Settings(openai=OpenAISettings(api_key="sk-test"))
    (cfg,) = recorded_runs
    assert cfg.devices == () and cfg.http_port == 8080 and cfg.openai.api_key == "sk-test"


def test_main_reads_existing_settings_and_ignores_the_config(tmp_path, recorded_runs, caplog):
    settings = Settings(openai=OpenAISettings(api_key="sk-file"), assistant=AssistantSettings(language="en"),
                        http_port=9292)
    write_settings(tmp_path, settings)
    path = tmp_path / "config.yaml"
    path.write_text("http_port: 9191\ndevices: []\n")
    caplog.set_level(logging.INFO)
    assert main(["--data", str(tmp_path), "--config", str(path)], env={"OPENAI_API_KEY": "sk-env"}) == 0
    (cfg,) = recorded_runs
    assert cfg.http_port == 9292 and cfg.openai.api_key == "sk-file" and cfg.assistant.language == "en"
    assert "config.yaml" in caplog.text and "ignor" in caplog.text
    assert SettingsStore(tmp_path / "settings.json").load() == settings


def test_main_does_not_log_secrets(tmp_path, recorded_runs, caplog, capsys):
    path = tmp_path / "config.yaml"
    path.write_text("devices:\n  - name: kitchen\n    host: 192.0.2.10\n    encryption_key_env: KEY\n")
    caplog.set_level(logging.DEBUG)
    env = {"OPENAI_API_KEY": "sk-marker-secret", "KEY": device_key(7)}
    assert main(["--data", str(tmp_path), "--config", str(path)], env=env) == 0
    assert main(["--data", str(tmp_path), "--config", str(path)], env=env) == 0
    out = capsys.readouterr()
    for text in (caplog.text, out.out, out.err):
        assert "sk-marker-secret" not in text and device_key(7) not in text


@pytest.mark.parametrize("env, expected", [({"VOICE_HUB_PASSWORD": "page password 1"}, "page password 1"),
                                           ({"VOICE_HUB_PASSWORD": ""}, None), ({}, None)])
def test_main_passes_the_page_password_from_the_environment(tmp_path, recorded_runs, passwords, caplog, capsys, env,
                                                            expected):
    caplog.set_level(logging.DEBUG)
    assert main(["--data", str(tmp_path)], env=env) == 0
    assert passwords == [(expected, frozenset())]
    out = capsys.readouterr()
    for text in (caplog.text, out.out, out.err, (tmp_path / "settings.json").read_text()):
        assert "page password 1" not in text


@pytest.mark.parametrize("value, expected", [
    ("Voice.Example.com", {"voice.example.com"}), (" voice.example.com , other.example.org:8443,, ", {
        "voice.example.com", "other.example.org"}), ("", set()),
])
def test_main_passes_the_accepted_host_names(tmp_path, recorded_runs, passwords, value, expected):
    assert main(["--data", str(tmp_path)], env={"VOICE_HUB_HOSTS": value}) == 0
    assert passwords == [(None, frozenset(expected))]


@pytest.mark.parametrize("value, expected", [("9393", 9393), (" 1024 ", 1024), ("65535", 65535), ("08081", 8081),
                                             ("", None), ("  ", None)])
def test_main_takes_the_port_from_the_environment(tmp_path, recorded_runs, ports, value, expected):
    assert main(["--data", str(tmp_path)], env={"VOICE_HUB_PORT": value}) == 0
    assert ports == [expected]
    assert SettingsStore(tmp_path / "settings.json").load().http_port == 8080


def test_main_without_a_port_variable_uses_the_settings_file(tmp_path, recorded_runs, ports):
    assert main(["--data", str(tmp_path)], env={}) == 0
    assert ports == [None]


@pytest.mark.parametrize("value", ["80", "1023", "65536", "0", "8o81", "8_081", "+8081", "-1", "8080.0", "٨٠٨١",
                                   pytest.param("9" * 5000, id="5000-nines"),
                                   pytest.param("0" * 4999 + "1", id="5000-digits-of-1")])
def test_main_refuses_a_port_variable_it_cannot_use(tmp_path, capsys, recorded_runs, value):
    assert main(["--data", str(tmp_path)], env={"VOICE_HUB_PORT": value}) == 2
    err = capsys.readouterr().err
    assert err == "error: VOICE_HUB_PORT: must be a whole number from 1024 to 65535\n"
    assert recorded_runs == [] and not (tmp_path / "settings.json").exists()


def test_main_says_how_to_start_after_a_bad_port_in_the_settings_file(tmp_path, capsys, recorded_runs):
    (tmp_path / "settings.json").write_text('{"version": 1, "http_port": 80}', encoding="utf-8")
    assert main(["--data", str(tmp_path)], env={}) == 2
    err = capsys.readouterr().err
    assert err.startswith(f"error: {tmp_path / 'settings.json'}: http_port: ") and err.count("\n") == 1
    assert "VOICE_HUB_PORT" in err and "Traceback" not in err
    assert recorded_runs == []


def test_main_starts_on_the_port_variable_despite_a_bad_port_in_the_settings_file(tmp_path, recorded_runs, ports,
                                                                                    caplog):
    (tmp_path / "settings.json").write_text('{"version": 1, "http_port": 80}', encoding="utf-8")
    assert main(["--data", str(tmp_path)], env={"VOICE_HUB_PORT": "9099"}) == 0
    assert ports == [9099] and recorded_runs[0].http_port == 9099
    assert "VOICE_HUB_PORT" in caplog.text


def test_main_names_the_config_file_when_seeding_refuses_a_value(tmp_path, capsys, recorded_runs):
    path = tmp_path / "config.yaml"
    path.write_text("devices: []\nhttp_port: 80\n")
    assert main(["--data", str(tmp_path / "data"), "--config", str(path)], env={}) == 2
    err = capsys.readouterr().err
    assert err == f"error: {path}: http_port: must be a whole number from 1024 to 65535\n"
    assert recorded_runs == [] and not (tmp_path / "data" / "settings.json").exists()


def test_main_reports_damaged_settings(tmp_path, capsys, recorded_runs):
    (tmp_path / "settings.json").write_text('{"version": 1, "openai": {')
    assert main(["--data", str(tmp_path)], env={}) == 2
    err = capsys.readouterr().err
    assert "settings.json" in err and "Traceback" not in err
    assert recorded_runs == []
    assert (tmp_path / "settings.json").read_text() == '{"version": 1, "openai": {'


def test_main_reports_unwritable_data_dir(tmp_path, capsys, recorded_runs):
    blocker = tmp_path / "file"
    blocker.write_text("")
    assert main(["--data", str(blocker / "data")], env={}) == 2
    assert "settings.json" in capsys.readouterr().err
    assert recorded_runs == []


def test_main_requires_data_argument(tmp_path, capsys):
    with pytest.raises(SystemExit) as exc:
        main([], env={})
    assert exc.value.code == 2
    with pytest.raises(SystemExit) as exc:
        main(["--config", str(tmp_path / "config.yaml")], env={})
    assert exc.value.code == 2


def test_dunder_main_exposes_the_entry_point():
    from voice_hub import __main__

    assert __main__.main is main


async def test_run_serves_health_and_stops_on_sigterm(store):
    cfg = config(http_port=unused_port())
    task = await serve(cfg, store)
    await terminate(task)
    with pytest.raises(OSError):
        socket.create_connection(("127.0.0.1", cfg.http_port), timeout=1.0).close()


async def test_run_serves_the_settings_api_behind_the_password(store, caplog):
    caplog.set_level(logging.INFO)
    cfg = config(http_port=unused_port())
    task = await serve(cfg, store, password="page password 1", hosts=frozenset({"voice.example.com"}))
    base = f"http://127.0.0.1:{cfg.http_port}"
    async with aiohttp.ClientSession(cookie_jar=aiohttp.CookieJar(unsafe=True)) as http:
        async with http.get(f"{base}/api/state") as resp:
            assert await resp.json() == {"password_set": True, "password_from_env": True, "logged_in": False}
        async with http.get(f"{base}/api/state", headers={"Host": "voice.example.com:8080"}) as resp:
            assert resp.status == 200
        async with http.get(f"{base}/api/state", headers={"Host": "evil.test"}) as resp:
            assert resp.status == 403
        async with http.get(f"{base}/api/settings") as resp:
            assert resp.status == 401
        async with http.post(f"{base}/api/login", json={"password": "page password 1"}) as resp:
            assert resp.status == 200
        async with http.put(f"{base}/api/settings", json={"openai": {"model": "gpt-x"}}) as resp:
            assert resp.status == 200 and (await resp.json())["settings"]["openai"]["model"] == "gpt-x"
        async with http.get(f"{base}/health") as resp:
            assert resp.status == 200
    await terminate(task)
    assert store.load().openai.model == "gpt-x"
    assert "VOICE_HUB_PASSWORD" in caplog.text and "page password 1" not in caplog.text


async def test_run_removes_signal_handlers_after_stopping(store):
    loop = asyncio.get_running_loop()
    cfg = config(http_port=unused_port())
    task = await serve(cfg, store)
    await terminate(task)
    assert not loop.remove_signal_handler(signal.SIGTERM)
    assert not loop.remove_signal_handler(signal.SIGINT)


async def test_run_looks_for_speakers_while_serving(store):
    cfg = config(http_port=unused_port())
    task = await serve(cfg, store)
    (discovery,) = FakeDiscovery.made
    assert discovery.trace == ["start"]
    await terminate(task)
    assert discovery.trace == ["start", "stop"]


async def test_run_wires_devices_and_stops_them(fake_devices, store):
    cfg = config(device_cfg("kitchen"), device_cfg("hall"), http_port=unused_port(), timing=Timing(first_turn_s=3.0))
    task = await serve(cfg, store)
    kitchen, hall = RecordingLink.created
    assert [kitchen.name, hall.name] == ["kitchen", "hall"]
    assert kitchen.cfg.encryption_key == device_key() and kitchen.cfg.host == "192.0.2.10"
    assert kitchen.trace == ["start"] and hall.trace == ["start"]
    kw = kitchen.conversation.kwargs
    assert kw["device"] is kitchen and kw["timing"]().first_turn_s == 3.0 and kw["debug_input"] is None
    assert kw["audio_url"]("abc") == f"http://hub.test:{cfg.http_port}/audio/abc.wav"
    assert isinstance(kw["session_factory"](), RealtimeSession)
    assert kw["streams"] is hall.conversation.kwargs["streams"]
    async with aiohttp.ClientSession() as http:
        async with http.get(f"http://127.0.0.1:{cfg.http_port}/health") as resp:
            assert resp.status == 503
            assert await resp.json() == {"devices": {"kitchen": False, "hall": False}}
    await terminate(task)
    assert kitchen.trace == ["start", "stop"] and hall.trace == ["start", "stop"]


async def test_run_leaves_speakers_without_a_key_offline(fake_devices, store, caplog):
    cfg = config(device_cfg("kitchen"), DeviceSettings(name="porch", host="192.0.2.12"), http_port=unused_port())
    task = await serve(cfg, store)
    assert [link.name for link in RecordingLink.created] == ["kitchen"]
    async with aiohttp.ClientSession() as http:
        async with http.get(f"http://127.0.0.1:{cfg.http_port}/health") as resp:
            assert await resp.json() == {"devices": {"kitchen": False}}
    assert "porch" in caplog.text
    await terminate(task)


async def test_run_applies_new_settings_to_new_sessions(fake_devices, store, monkeypatch):
    hubs: list[Hub] = []
    original = Hub.__init__

    def spy(self, *args, **kwargs) -> None:
        original(self, *args, **kwargs)
        hubs.append(self)

    monkeypatch.setattr(Hub, "__init__", spy)
    cfg = config(device_cfg("kitchen"), http_port=unused_port())
    store.save(cfg)
    task = await serve(cfg, store)
    (hub,) = hubs
    kw = RecordingLink.created[0].conversation.kwargs
    assert kw["session_factory"]()._cfg.model == cfg.openai.model
    await hub.update_settings({"openai": {"model": "gpt-other"}, "timing": {"first_turn_s": 2.5},
                               "devices": [{"name": "kitchen"}, {"name": "hall", "host": "192.0.2.11",
                                                                 "key": device_key(2)}]})
    assert kw["session_factory"]()._cfg.model == "gpt-other" and kw["timing"]().first_turn_s == 2.5
    assert [link.name for link in RecordingLink.created] == ["kitchen", "hall"]
    async with aiohttp.ClientSession() as http:
        async with http.get(f"http://127.0.0.1:{cfg.http_port}/health") as resp:
            assert await resp.json() == {"devices": {"kitchen": False, "hall": False}}
    assert store.load().openai.model == "gpt-other"
    await terminate(task)
    assert RecordingLink.created[1].trace == ["start", "stop"]


async def test_health_counts_a_speaker_that_failed_to_start_as_offline(fake_devices, store, monkeypatch):
    class BrokenHall(RecordingLink):
        def __init__(self, cfg, make_conversation) -> None:
            if cfg.name == "hall":
                raise OSError("cannot build hall")
            super().__init__(cfg, make_conversation)

    monkeypatch.setattr(registry, "DeviceLink", BrokenHall)
    cfg = config(device_cfg("kitchen"), device_cfg("hall"), DeviceSettings(name="porch", host="192.0.2.12"),
                 http_port=unused_port())
    task = await serve(cfg, store)
    RecordingLink.created[0].connected = True
    async with aiohttp.ClientSession() as http:
        async with http.get(f"http://127.0.0.1:{cfg.http_port}/health") as resp:
            assert resp.status == 503
            assert await resp.json() == {"devices": {"kitchen": True, "hall": False}}
    await terminate(task)


async def test_run_serves_streams_of_its_conversations(fake_devices, store):
    cfg = config(device_cfg(), http_port=unused_port())
    task = await serve(cfg, store)
    streams = RecordingLink.created[0].conversation.kwargs["streams"]
    stream = streams.create()
    stream.write(b"\x05\x06")
    stream.finish()
    async with aiohttp.ClientSession() as http:
        async with http.get(f"http://127.0.0.1:{cfg.http_port}/audio/{stream.id}.wav") as resp:
            assert resp.status == 200
            assert await resp.read() == wav_header(DEVICE_RATE) + b"\x05\x06"
    await terminate(task)


async def test_run_passes_debug_input_at_device_rate(fake_devices, store, tmp_path):
    native = tmp_path / "native.wav"
    slow = tmp_path / "slow.wav"
    write_wav(native, DEVICE_RATE, 1600)
    write_wav(slow, 8000, 800)
    cfg = config(
        device_cfg("kitchen", debug_input_wav=native),
        device_cfg("hall", debug_input_wav=slow),
        device_cfg("porch"),
        http_port=unused_port(),
    )
    task = await serve(cfg, store)
    kitchen, hall, porch = (link.conversation.kwargs["debug_input"] for link in RecordingLink.created)
    assert kitchen == b"\x01\x00" * 1600
    assert 3000 <= len(hall) <= 3400 and len(hall) % 2 == 0
    assert porch is None
    await terminate(task)


async def test_run_starts_nothing_when_the_port_is_taken(fake_devices, store):
    with socket.socket() as busy:
        busy.bind(("0.0.0.0", 0))
        busy.listen()
        port = busy.getsockname()[1]
        with pytest.raises(StartError, match=f"port {port} cannot be used") as info:
            await asyncio.wait_for(run(store, config(device_cfg(), http_port=port)), 5.0)
    message = str(info.value)
    assert "http_port" in message and str(store.path) in message
    assert "VOICE_HUB_PORT" in message and ".env" in message
    assert RecordingLink.created == [] and FakeDiscovery.made[0].trace in ([], ["stop"])


async def test_a_taken_port_from_the_environment_says_to_change_the_variable(fake_devices, store):
    with socket.socket() as busy:
        busy.bind(("0.0.0.0", 0))
        busy.listen()
        port = busy.getsockname()[1]
        with pytest.raises(StartError, match=f"port {port} cannot be used") as info:
            await asyncio.wait_for(run(store, config(device_cfg(), http_port=unused_port()), env_port=port), 5.0)
    message = str(info.value)
    assert "VOICE_HUB_PORT" in message and ".env" in message
    assert "http_port" not in message and str(store.path) not in message


async def test_run_listens_on_the_port_from_the_environment(fake_devices, store, caplog):
    caplog.set_level(logging.INFO)
    port = unused_port()
    cfg = config(device_cfg(), http_port=unused_port())
    task = await serve(cfg, store, env_port=port)
    kw = RecordingLink.created[0].conversation.kwargs
    assert kw["audio_url"]("abc") == f"http://hub.test:{port}/audio/abc.wav"
    assert f"listening on port {port}" in caplog.text and "VOICE_HUB_PORT" in caplog.text
    await terminate(task)


async def test_run_waits_for_the_network_to_learn_its_address(fake_devices, store, monkeypatch, caplog):
    answers = [OSError(101, "Network is unreachable"), OSError(101, "Network is unreachable"), "192.0.2.7"]

    def flaky() -> str:
        answer = answers.pop(0)
        if isinstance(answer, Exception):
            raise answer
        return answer

    monkeypatch.setattr(app, "local_ip", flaky)
    monkeypatch.setattr(app, "NETWORK_RETRY_S", 0.02)
    cfg = replace(config(device_cfg(), http_port=unused_port()), advertise_host=None)
    task = await serve(cfg, store)
    kw = RecordingLink.created[0].conversation.kwargs
    assert kw["audio_url"]("abc") == f"http://192.0.2.7:{cfg.http_port}/audio/abc.wav"
    waits = [r for r in caplog.records if r.levelno == logging.WARNING and "Network is unreachable" in r.getMessage()]
    assert len(waits) == 1 and answers == []
    await terminate(task)
    assert "Traceback" not in caplog.text


async def test_run_stops_cleanly_while_waiting_for_the_network(fake_devices, store, monkeypatch):
    def offline() -> str:
        raise OSError(101, "Network is unreachable")

    monkeypatch.setattr(app, "local_ip", offline)
    monkeypatch.setattr(app, "NETWORK_RETRY_S", 0.02)
    cfg = replace(config(device_cfg(), http_port=unused_port()), advertise_host=None)
    task = asyncio.create_task(run(store, cfg))
    await asyncio.sleep(0.1)
    assert not task.done()
    await terminate(task)
    assert RecordingLink.created == []
    loop = asyncio.get_running_loop()
    assert not loop.remove_signal_handler(signal.SIGTERM)


async def test_a_set_server_address_needs_no_network_lookup(fake_devices, store, monkeypatch):
    def unexpected() -> str:
        raise AssertionError("local_ip was called")

    monkeypatch.setattr(app, "local_ip", unexpected)
    cfg = config(device_cfg(), http_port=unused_port())
    task = await serve(cfg, store)
    assert RecordingLink.created[0].conversation.kwargs["audio_url"]("abc").startswith("http://hub.test:")
    await terminate(task)


async def test_an_ipv6_server_address_is_put_in_brackets(fake_devices, store):
    cfg = replace(config(device_cfg(), http_port=unused_port()), advertise_host="2001:db8::5")
    task = await serve(cfg, store)
    kw = RecordingLink.created[0].conversation.kwargs
    assert kw["audio_url"]("abc") == f"http://[2001:db8::5]:{cfg.http_port}/audio/abc.wav"
    await terminate(task)


def test_main_explains_a_busy_port_in_one_line_and_exits_with_2(tmp_path, capsys, fake_devices):
    with socket.socket() as busy:
        busy.bind(("0.0.0.0", 0))
        busy.listen()
        port = busy.getsockname()[1]
        write_settings(tmp_path, config(http_port=port))
        assert main(["--data", str(tmp_path)], env={}) == 2
    err = capsys.readouterr().err
    assert err.startswith(f"error: port {port} cannot be used (") and err.count("\n") == 1
    assert "Traceback" not in err and "settings.json" in err and "VOICE_HUB_PORT" in err


async def test_run_rejects_unreadable_debug_wav_before_starting(fake_devices, store, tmp_path):
    cfg = config(device_cfg(debug_input_wav=tmp_path / "missing.wav"), http_port=unused_port())
    with pytest.raises(ConfigError, match="missing.wav"):
        await run(store, cfg)
    assert RecordingLink.created == []
