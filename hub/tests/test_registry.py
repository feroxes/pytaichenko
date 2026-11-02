import asyncio
import base64
import logging
from dataclasses import replace

import pytest
from aioesphomeapi import APIConnectionError, DeviceInfo, InvalidEncryptionKeyAPIError, MediaPlayerEntityState

from device_fakes import (
    AdoptClient,
    AdoptFactory,
    ConnectingReconnect,
    FakeClients,
    gone_now,
    keyed_now,
    keyless_now,
)
from fakes import SETTINGS, E, FakeSession, FakeStreams, audio_url, wait_for
from voice_hub import adopt as adopt_module
from voice_hub import device, registry
from voice_hub.adopt import AdoptError
from voice_hub.config import Timing
from voice_hub.device import VolumeError, WakeWordError, WakeWords
from voice_hub.realtime import AudioDelta, ResponseCreated, ResponseDone, SpeechStarted, SpeechStopped, TranscriptDone
from voice_hub.registry import DeviceRegistry, Hub, link_factory
from voice_hub.settings import DeviceSettings, OpenAISettings, Settings, SettingsError, SettingsStore


def key(n: int) -> str:
    return base64.b64encode(bytes([n]) * 32).decode("ascii")


def dev(name: str, host: str, n: int | None = 1, **kw) -> DeviceSettings:
    return DeviceSettings(name=name, host=host, key=key(n) if n is not None else None, **kw)


KITCHEN = dev("kitchen", "192.0.2.10", 1)
HALL = dev("hall", "192.0.2.11", 2)
PORCH = dev("porch", "192.0.2.12", None)


class StubLink:
    def __init__(self, device: DeviceSettings, journal: list) -> None:
        self.device = device
        self.name = device.name
        self.connected = False
        self.wake_words = WakeWords()
        self.journal = journal
        self.fail_stop: Exception | None = None
        self.chosen: list[str] = []
        self.level: float | None = 0.5
        self.levels: list[float] = []

    @property
    def player_volume(self) -> float | None:
        return self.level

    async def start(self) -> None:
        self.journal.append(("start", self.name, self.device.host))
        self.connected = True

    async def stop(self) -> None:
        self.journal.append(("stop", self.name, self.device.host))
        self.connected = False
        if self.fail_stop is not None:
            raise self.fail_stop

    async def set_wake_word(self, wake_word_id: str) -> None:
        self.chosen.append(wake_word_id)

    def change_volume(self, level: float) -> None:
        self.levels.append(level)
        self.level = level


class Links:
    def __init__(self) -> None:
        self.journal: list = []
        self.made: list[StubLink] = []
        self.broken: set[str] = set()

    def __call__(self, device: DeviceSettings) -> StubLink:
        if device.name in self.broken:
            raise OSError(f"cannot build {device.name}")
        link = StubLink(device, self.journal)
        self.made.append(link)
        return link


@pytest.fixture
def links():
    return Links()


@pytest.fixture(autouse=True)
def quick_probe(monkeypatch):
    monkeypatch.setattr(adopt_module, "PROBE_DELAY_S", 0.0)


async def test_reconcile_starts_speakers_that_have_a_key(links, caplog):
    reg = DeviceRegistry(links)
    await reg.reconcile(Settings(devices=(KITCHEN, PORCH, HALL)))
    assert links.journal == [("start", "kitchen", "192.0.2.10"), ("start", "hall", "192.0.2.11")]
    assert [link.name for link in reg] == ["kitchen", "hall"]
    assert reg.link("kitchen") is links.made[0] and reg.link("porch") is None and reg.link("nobody") is None
    assert "porch" in caplog.text


async def test_status_lists_every_speaker_in_order(links):
    reg = DeviceRegistry(links)
    await reg.reconcile(Settings(devices=(KITCHEN, PORCH, HALL)))
    links.made[0].wake_words = WakeWords(available=(("okay_nabu", "Okay Nabu"),), active=("okay_nabu",))
    links.made[0].level = 0.78
    links.made[1].connected = False
    assert reg.status() == [
        {"name": "kitchen", "host": "192.0.2.10", "port": 6053, "key_set": True, "connected": True,
         "wake_words": {"available": [("okay_nabu", "Okay Nabu")], "active": ["okay_nabu"]}, "volume": 78},
        {"name": "porch", "host": "192.0.2.12", "port": 6053, "key_set": False, "connected": False,
         "wake_words": {"available": [], "active": []}, "volume": None},
        {"name": "hall", "host": "192.0.2.11", "port": 6053, "key_set": True, "connected": False,
         "wake_words": {"available": [], "active": []}, "volume": None},
    ]


@pytest.mark.parametrize("level, shown", [
    (0.78, 78), (0.0, 0), (1.0, 100), (0.333, 33), (0.666, 67), (1.2, 100), (-0.1, 0), (None, None),
])
async def test_status_shows_the_volume_in_whole_percent(links, level, shown):
    reg = DeviceRegistry(links)
    await reg.reconcile(Settings(devices=(KITCHEN,)))
    links.made[0].level = level
    (kitchen,) = reg.status()
    assert kitchen["volume"] == shown and (shown is None or type(kitchen["volume"]) is int)


async def test_status_has_no_keys(links):
    reg = DeviceRegistry(links)
    await reg.reconcile(Settings(devices=(KITCHEN, HALL)))
    assert key(1) not in repr(reg.status()) and key(2) not in repr(reg.status())


async def test_health_counts_speakers_that_have_a_key(links):
    links.broken = {"hall"}
    reg = DeviceRegistry(links)
    await reg.reconcile(Settings(devices=(KITCHEN, PORCH, HALL)))
    assert reg.health() == {"kitchen": True, "hall": False}
    links.made[0].connected = False
    assert reg.health() == {"kitchen": False, "hall": False}


async def test_same_settings_keep_the_links(links):
    reg = DeviceRegistry(links)
    await reg.reconcile(Settings(devices=(KITCHEN, HALL)))
    first = list(reg)
    await reg.reconcile(Settings(devices=(KITCHEN, HALL), openai=OpenAISettings(model="other")))
    assert list(reg) == first and len(links.journal) == 2


@pytest.mark.parametrize(
    "change", [{"host": "192.0.2.20"}, {"port": 6054}, {"key": key(9)}, {"debug_input_wav": "/tmp/q.wav"}]
)
async def test_changed_speaker_gets_a_whole_new_link(links, change):
    reg = DeviceRegistry(links)
    await reg.reconcile(Settings(devices=(KITCHEN, HALL)))
    old_kitchen, hall = links.made
    links.journal.clear()
    await reg.reconcile(Settings(devices=(replace(KITCHEN, **change), HALL)))
    new_kitchen = reg.link("kitchen")
    assert new_kitchen is not old_kitchen and new_kitchen.device == replace(KITCHEN, **change)
    assert [entry[:2] for entry in links.journal] == [("stop", "kitchen"), ("start", "kitchen")]
    assert reg.link("hall") is hall


async def test_renamed_speaker_is_replaced(links):
    reg = DeviceRegistry(links)
    await reg.reconcile(Settings(devices=(KITCHEN,)))
    links.journal.clear()
    await reg.reconcile(Settings(devices=(replace(KITCHEN, name="cuisine"),)))
    assert links.journal == [("stop", "kitchen", "192.0.2.10"), ("start", "cuisine", "192.0.2.10")]
    assert [link.name for link in reg] == ["cuisine"]


async def test_removed_speaker_is_stopped_and_others_kept(links):
    reg = DeviceRegistry(links)
    await reg.reconcile(Settings(devices=(KITCHEN, HALL)))
    kitchen, hall = links.made
    links.journal.clear()
    await reg.reconcile(Settings(devices=(HALL,)))
    assert links.journal == [("stop", "kitchen", "192.0.2.10")]
    assert list(reg) == [hall] and reg.link("kitchen") is None


async def test_forgotten_key_stops_the_link(links):
    reg = DeviceRegistry(links)
    await reg.reconcile(Settings(devices=(KITCHEN,)))
    await reg.reconcile(Settings(devices=(replace(KITCHEN, key=None),)))
    assert links.journal[-1] == ("stop", "kitchen", "192.0.2.10") and list(reg) == []
    assert reg.status()[0]["key_set"] is False


async def test_speaker_that_cannot_be_built_is_skipped_and_retried(links, caplog):
    links.broken = {"kitchen"}
    reg = DeviceRegistry(links)
    await reg.reconcile(Settings(devices=(KITCHEN, HALL)))
    assert [link.name for link in reg] == ["hall"] and "kitchen" in caplog.text
    assert [s["name"] for s in reg.status()] == ["kitchen", "hall"]
    links.broken = set()
    await reg.reconcile(Settings(devices=(KITCHEN, HALL)))
    assert [link.name for link in reg] == ["kitchen", "hall"]
    assert links.journal.count(("start", "hall", "192.0.2.11")) == 1


async def test_speaker_that_fails_to_start_is_stopped_and_retried(links, caplog, monkeypatch):
    original = StubLink.start

    async def broken(self) -> None:
        await original(self)
        if len(links.made) == 1:
            raise OSError("no socket")

    monkeypatch.setattr(StubLink, "start", broken)
    reg = DeviceRegistry(links)
    await reg.reconcile(Settings(devices=(KITCHEN,)))
    assert list(reg) == [] and "kitchen" in caplog.text
    assert links.journal == [("start", "kitchen", "192.0.2.10"), ("stop", "kitchen", "192.0.2.10")]
    await reg.reconcile(Settings(devices=(KITCHEN,)))
    assert [link.name for link in reg] == ["kitchen"] and len(links.made) == 2


async def test_failed_stop_is_logged_and_does_not_stop_the_rest(links, caplog):
    reg = DeviceRegistry(links)
    await reg.reconcile(Settings(devices=(KITCHEN, HALL)))
    kitchen, hall = links.made
    kitchen.fail_stop = OSError("boom")
    await reg.reconcile(Settings(devices=(HALL, PORCH)))
    assert "could not stop kitchen" in caplog.text
    assert list(reg) == [hall] and hall.connected


async def test_concurrent_reconciles_start_each_speaker_once(links):
    reg = DeviceRegistry(links)
    settings = Settings(devices=(KITCHEN, HALL))
    await asyncio.gather(reg.reconcile(settings), reg.reconcile(settings), reg.reconcile(settings))
    assert len(links.made) == 2


async def test_close_stops_everything_and_ignores_later_changes(links, caplog):
    reg = DeviceRegistry(links)
    await reg.reconcile(Settings(devices=(KITCHEN, HALL)))
    links.made[0].fail_stop = OSError("boom")
    await reg.close()
    assert sorted(entry[:2] for entry in links.journal if entry[0] == "stop") == [("stop", "hall"), ("stop", "kitchen")]
    assert list(reg) == [] and "could not stop kitchen" in caplog.text
    await reg.reconcile(Settings(devices=(KITCHEN, HALL)))
    assert list(reg) == [] and len(links.made) == 2


def make_store(tmp_path, settings: Settings) -> SettingsStore:
    store = SettingsStore(tmp_path / "settings.json")
    store.save(settings)
    return store


async def test_hub_starts_from_the_saved_settings(tmp_path, links):
    settings = Settings(devices=(KITCHEN,))
    hub = Hub(make_store(tmp_path, settings), settings, DeviceRegistry(links))
    await hub.start()
    assert hub.settings is settings and [link.name for link in hub.registry] == ["kitchen"]
    assert hub.status() == hub.registry.status()
    await hub.close()
    assert links.journal[-1] == ("stop", "kitchen", "192.0.2.10")


async def test_update_settings_saves_then_applies(tmp_path, links):
    store = make_store(tmp_path, Settings(devices=(KITCHEN,)))
    hub = Hub(store, Settings(devices=(KITCHEN,)), DeviceRegistry(links))
    await hub.start()
    new = await hub.update_settings({"devices": [{"name": "kitchen"}, {"name": "hall", "host": "192.0.2.11",
                                                                          "key": key(2)}],
                                     "openai": {"model": "gpt-other"}})
    assert hub.settings is new and store.load() == new
    assert new.openai.model == "gpt-other" and new.devices[0] == KITCHEN
    assert [link.name for link in hub.registry] == ["kitchen", "hall"] and len(links.made) == 2
    await hub.close()


async def test_bad_patch_changes_nothing(tmp_path, links):
    store = make_store(tmp_path, Settings(devices=(KITCHEN,)))
    before = store.path.read_bytes()
    hub = Hub(store, store.load(), DeviceRegistry(links))
    await hub.start()
    with pytest.raises(SettingsError):
        await hub.update_settings({"timing": {"first_turn_s": 0}})
    assert store.path.read_bytes() == before and hub.settings == store.load() and len(links.journal) == 1
    await hub.close()


async def test_failed_save_changes_nothing(tmp_path, links, monkeypatch):
    store = make_store(tmp_path, Settings(devices=(KITCHEN,)))
    hub = Hub(store, store.load(), DeviceRegistry(links))
    await hub.start()
    before = hub.settings

    def broken(settings) -> None:
        raise SettingsError("settings.json: cannot be saved (No space left on device)")

    monkeypatch.setattr(store, "save", broken)
    with pytest.raises(SettingsError):
        await hub.update_settings({"devices": []})
    assert hub.settings is before and [link.name for link in hub.registry] == ["kitchen"]
    await hub.close()


async def test_change_applies_any_function(tmp_path, links):
    store = make_store(tmp_path, Settings())
    hub = Hub(store, Settings(), DeviceRegistry(links))
    new = await hub.change(lambda s: replace(s, password_hash="scrypt$marker"))
    assert hub.settings is new and store.load().password_hash == "scrypt$marker"


async def test_set_wake_word_goes_to_the_speaker(tmp_path, links):
    store = make_store(tmp_path, Settings(devices=(KITCHEN, PORCH)))
    hub = Hub(store, store.load(), DeviceRegistry(links))
    await hub.start()
    await hub.set_wake_word("kitchen", "hey_mycroft")
    assert links.made[0].chosen == ["hey_mycroft"]
    for name in ("porch", "nobody"):
        with pytest.raises(WakeWordError) as err:
            await hub.set_wake_word(name, "hey_mycroft")
        assert err.value.code == "unknown_speaker"
    await hub.close()


async def test_set_volume_goes_to_the_speaker_as_a_level(tmp_path, links):
    store = make_store(tmp_path, Settings(devices=(KITCHEN, PORCH)))
    hub = Hub(store, store.load(), DeviceRegistry(links))
    await hub.start()
    hub.set_volume("kitchen", 35)
    hub.set_volume("kitchen", 100)
    assert links.made[0].levels == [0.35, 1.0] and hub.status()[0]["volume"] == 100
    for name in ("porch", "nobody"):
        with pytest.raises(VolumeError) as err:
            hub.set_volume(name, 35)
        assert err.value.code == "unknown_speaker"
    await hub.close()


async def test_adopt_saves_the_new_speaker_and_connects_with_its_key(tmp_path, links, caplog):
    caplog.set_level(logging.DEBUG)
    store = make_store(tmp_path, Settings(devices=(KITCHEN,)))
    client = AdoptClient()
    factory = AdoptFactory(client)
    hub = Hub(store, store.load(), DeviceRegistry(links), client_factory=factory)
    await hub.start()
    assert await hub.adopt("192.0.2.30", 6055) == "speaker-a1b2c3"
    new_key = client.keys[0].decode("ascii")
    expected = DeviceSettings(name="speaker-a1b2c3", host="192.0.2.30", port=6055, key=new_key)
    assert store.load().devices == (KITCHEN, expected) and hub.settings.devices == (KITCHEN, expected)
    assert hub.registry.link("speaker-a1b2c3").device == expected
    assert new_key not in caplog.text
    await hub.close()


async def test_adopt_saves_the_entry_before_the_key_is_sent(tmp_path, links):
    store = make_store(tmp_path, Settings(devices=(KITCHEN,)))
    client = AdoptClient()
    on_disk: list = []
    client.on_set = lambda sent: on_disk.append(store.load().devices)
    hub = Hub(store, store.load(), DeviceRegistry(links), client_factory=AdoptFactory(client))
    await hub.start()
    await hub.adopt("192.0.2.30", 6053)
    (devices,) = on_disk
    assert [d.name for d in devices] == ["kitchen", "speaker-a1b2c3"]
    assert devices[1].key == client.keys[0].decode("ascii") and len(links.made) == 2
    await hub.close()


async def test_adopt_with_a_failed_save_never_sends_a_key(tmp_path, links, monkeypatch):
    store = make_store(tmp_path, Settings(devices=(KITCHEN,)))
    before = store.path.read_bytes()
    client = AdoptClient()
    hub = Hub(store, store.load(), DeviceRegistry(links), client_factory=AdoptFactory(client))
    await hub.start()
    settings = hub.settings

    def broken(new) -> None:
        raise SettingsError("settings.json: cannot be saved (No space left on device)")

    monkeypatch.setattr(store, "save", broken)
    with pytest.raises(SettingsError):
        await hub.adopt("192.0.2.30", 6053)
    assert client.keys == [] and "set_key" not in client.calls and client.calls[-1] == "disconnect"
    assert hub.settings is settings and store.path.read_bytes() == before and len(links.made) == 1
    await hub.close()


def refusing() -> AdoptClient:
    client = AdoptClient()
    client.accept = False
    return client


def dropping() -> AdoptClient:
    client = AdoptClient()
    client.fail_set = APIConnectionError("Connection is closed")
    return client


@pytest.mark.parametrize("clients", [
    pytest.param(lambda: (refusing(),), id="speaker refuses"),
    pytest.param(lambda: (dropping(), keyless_now()), id="answer lost, still keyless"),
])
async def test_refused_adoption_rolls_the_entry_back(tmp_path, links, clients):
    store = make_store(tmp_path, Settings(devices=(KITCHEN,)))
    before = store.path.read_bytes()
    made = clients()
    hub = Hub(store, store.load(), DeviceRegistry(links), client_factory=AdoptFactory(*made))
    await hub.start()
    settings = hub.settings
    with pytest.raises(AdoptError) as err:
        await hub.adopt("192.0.2.30", 6053)
    assert err.value.code == "refused" and made[0].keys != []
    assert store.path.read_bytes() == before and hub.settings == settings
    assert len(links.made) == 1 and [s["name"] for s in hub.status()] == ["kitchen"]
    await hub.close()


@pytest.mark.parametrize("probe", [keyed_now, gone_now], ids=["now has a key", "unreachable"])
async def test_lost_answer_keeps_the_entry_unless_the_speaker_is_still_keyless(tmp_path, links, probe):
    store = make_store(tmp_path, Settings(devices=(KITCHEN,)))
    client = dropping()
    hub = Hub(store, store.load(), DeviceRegistry(links), client_factory=AdoptFactory(client, probe()))
    await hub.start()
    assert await hub.adopt("192.0.2.30", 6053) == "speaker-a1b2c3"
    expected = DeviceSettings(name="speaker-a1b2c3", host="192.0.2.30", key=client.keys[0].decode("ascii"))
    assert store.load().devices == (KITCHEN, expected) and hub.settings.devices == (KITCHEN, expected)
    assert hub.registry.link("speaker-a1b2c3").device == expected
    await hub.close()


async def test_failed_rollback_is_logged(tmp_path, links, monkeypatch, caplog):
    store = make_store(tmp_path, Settings(devices=(KITCHEN,)))
    hub = Hub(store, store.load(), DeviceRegistry(links), client_factory=AdoptFactory(refusing()))
    await hub.start()
    settings = hub.settings
    original = store.save
    calls: list = []

    def flaky(new) -> None:
        calls.append(new)
        if len(calls) > 1:
            raise SettingsError("settings.json: cannot be saved (Read-only file system)")
        original(new)

    monkeypatch.setattr(store, "save", flaky)
    with pytest.raises(AdoptError):
        await hub.adopt("192.0.2.30", 6053)
    assert hub.settings == settings and "could not take back" in caplog.text
    await hub.close()


async def test_adopt_replaces_an_offline_speaker_at_the_same_address(tmp_path, links):
    old = dev("speaker-a1b2c3", "192.0.2.30", 5)
    store = make_store(tmp_path, Settings(devices=(old, KITCHEN)))
    client = AdoptClient()
    hub = Hub(store, store.load(), DeviceRegistry(links), client_factory=AdoptFactory(client))
    await hub.start()
    links.made[0].connected = False
    await hub.adopt("192.0.2.30", 6053)
    new = DeviceSettings(name="speaker-a1b2c3", host="192.0.2.30", key=client.keys[0].decode("ascii"))
    assert store.load().devices == (new, KITCHEN)
    assert ("stop", "speaker-a1b2c3", "192.0.2.30") in links.journal
    assert hub.registry.link("speaker-a1b2c3").device == new
    await hub.close()


async def test_adopt_replaces_a_keyless_speaker_at_the_same_address(tmp_path, links):
    old = dev("speaker-a1b2c3", "192.0.2.30", None)
    store = make_store(tmp_path, Settings(devices=(old,)))
    client = AdoptClient()
    hub = Hub(store, store.load(), DeviceRegistry(links), client_factory=AdoptFactory(client))
    await hub.start()
    await hub.adopt("192.0.2.30", 6053)
    assert store.load().devices[0].key == client.keys[0].decode("ascii")
    await hub.close()


@pytest.mark.parametrize("case", ["connected", "other address"])
async def test_adopt_refuses_a_name_that_is_taken(tmp_path, links, case):
    old = dev("speaker-a1b2c3", "192.0.2.30" if case == "connected" else "192.0.2.99", 5)
    store = make_store(tmp_path, Settings(devices=(old,)))
    before = store.path.read_bytes()
    client = AdoptClient()
    hub = Hub(store, store.load(), DeviceRegistry(links), client_factory=AdoptFactory(client))
    await hub.start()
    links.made[0].connected = case == "connected"
    with pytest.raises(AdoptError) as err:
        await hub.adopt("192.0.2.30", 6053)
    assert err.value.code == "name_taken" and "speaker-a1b2c3" in str(err.value)
    assert client.keys == [] and store.path.read_bytes() == before and hub.settings.devices == (old,)
    assert links.journal == [("start", "speaker-a1b2c3", old.host)]
    await hub.close()


@pytest.mark.parametrize("refusal", ["not provisionable", "rejects zero key"])
async def test_adopting_a_speaker_that_has_a_key_changes_nothing(tmp_path, links, refusal):
    owned = dev("speaker-a1b2c3", "192.0.2.30", 5)
    store = make_store(tmp_path, Settings(devices=(owned,)))
    before = store.path.read_bytes()
    client = AdoptClient(DeviceInfo(name="speaker-a1b2c3", api_encryption_provisionable=False))
    if refusal == "rejects zero key":
        client.fail_connect = InvalidEncryptionKeyAPIError("Invalid encryption key", "speaker-a1b2c3")
    hub = Hub(store, store.load(), DeviceRegistry(links), client_factory=AdoptFactory(client))
    await hub.start()
    with pytest.raises(AdoptError) as err:
        await hub.adopt("192.0.2.30", 6053)
    assert err.value.code == "has_key" and "already has a key" in str(err.value)
    assert client.keys == [] and store.path.read_bytes() == before
    assert hub.settings.devices == (owned,) and links.journal == [("start", "speaker-a1b2c3", "192.0.2.30")]
    assert key(5) not in str(err.value)
    await hub.close()


@pytest.mark.parametrize("host, port", [("", 6053), ("  ", 6053), ("192.0.2.30", 0), ("192.0.2.30", 70000),
                                        ("192.0.2.30", "6053"), (None, 6053)])
async def test_adopt_checks_the_address_before_connecting(tmp_path, links, host, port):
    store = make_store(tmp_path, Settings())
    factory = AdoptFactory(AdoptClient())
    hub = Hub(store, Settings(), DeviceRegistry(links), client_factory=factory)
    with pytest.raises(SettingsError):
        await hub.adopt(host, port)
    assert factory.args == []


async def test_add_with_a_key_saves_the_speakers_own_name(tmp_path, links, caplog):
    caplog.set_level(logging.DEBUG)
    store = make_store(tmp_path, Settings(devices=(KITCHEN,)))
    client = AdoptClient(DeviceInfo(name="speaker-d4e5f6", api_encryption_provisionable=False))
    factory = AdoptFactory(client)
    hub = Hub(store, store.load(), DeviceRegistry(links), client_factory=factory)
    await hub.start()
    assert await hub.add(" 192.0.2.31 ", 6054, key(7)) == "speaker-d4e5f6"
    expected = DeviceSettings(name="speaker-d4e5f6", host="192.0.2.31", port=6054, key=key(7))
    assert factory.args == [("192.0.2.31", 6054, key(7))]
    assert store.load().devices == (KITCHEN, expected) and hub.settings.devices == (KITCHEN, expected)
    assert hub.registry.link("speaker-d4e5f6").device == expected
    assert client.keys == [] and key(7) not in caplog.text
    await hub.close()


@pytest.mark.parametrize("host, port, value, field", [
    ("192.0.2.31", 6053, None, "key"), ("192.0.2.31", 6053, "", "key"), ("192.0.2.31", 6053, "c2hvcnQ=", "key"),
    ("192.0.2.31", 6053, 5, "key"), ("", 6053, key(7), "host"), ("192.0.2.31", 0, key(7), "port"),
])
async def test_add_checks_its_input_before_connecting(tmp_path, links, host, port, value, field):
    store = make_store(tmp_path, Settings())
    factory = AdoptFactory(AdoptClient())
    hub = Hub(store, Settings(), DeviceRegistry(links), client_factory=factory)
    with pytest.raises(SettingsError) as err:
        await hub.add(host, port, value)
    assert str(err.value).startswith(field) and factory.args == []


async def test_add_with_a_wrong_key_saves_nothing(tmp_path, links):
    store = make_store(tmp_path, Settings(devices=(KITCHEN,)))
    before = store.path.read_bytes()
    client = AdoptClient()
    client.fail_connect = InvalidEncryptionKeyAPIError("Invalid encryption key", "speaker-a1b2c3")
    hub = Hub(store, store.load(), DeviceRegistry(links), client_factory=AdoptFactory(client))
    await hub.start()
    with pytest.raises(AdoptError) as err:
        await hub.add("192.0.2.31", 6053, key(7))
    assert err.value.code == "wrong_key" and store.path.read_bytes() == before
    assert [link.name for link in hub.registry] == ["kitchen"]
    await hub.close()


@pytest.mark.parametrize("case", ["connected", "other address"])
async def test_add_refuses_a_name_that_is_taken(tmp_path, links, case):
    old = dev("speaker-d4e5f6", "192.0.2.31" if case == "connected" else "192.0.2.99", 5)
    store = make_store(tmp_path, Settings(devices=(old,)))
    before = store.path.read_bytes()
    client = AdoptClient(DeviceInfo(name="speaker-d4e5f6", api_encryption_provisionable=False))
    hub = Hub(store, store.load(), DeviceRegistry(links), client_factory=AdoptFactory(client))
    await hub.start()
    links.made[0].connected = case == "connected"
    with pytest.raises(AdoptError) as err:
        await hub.add("192.0.2.31", 6053, key(7))
    assert err.value.code == "name_taken" and store.path.read_bytes() == before
    await hub.close()


async def test_add_replaces_an_offline_speaker_at_the_same_address(tmp_path, links):
    old = dev("speaker-d4e5f6", "192.0.2.31", 5)
    store = make_store(tmp_path, Settings(devices=(old,)))
    client = AdoptClient(DeviceInfo(name="speaker-d4e5f6", api_encryption_provisionable=False))
    hub = Hub(store, store.load(), DeviceRegistry(links), client_factory=AdoptFactory(client))
    await hub.start()
    links.made[0].connected = False
    await hub.add("192.0.2.31", 6053, key(7))
    new = DeviceSettings(name="speaker-d4e5f6", host="192.0.2.31", key=key(7))
    assert store.load().devices == (new,) and hub.registry.link("speaker-d4e5f6").device == new
    await hub.close()


FAST = Timing(
    mic_holdoff_s=0.0,
    first_turn_s=5.0,
    follow_up_s=5.0,
    max_speech_s=5.0,
    response_timeout_s=5.0,
    session_idle_s=0.3,
    mic_stop_grace_s=0.5,
    ready_check_s=1.0,
)


class Opened:
    def __init__(self) -> None:
        self.configs: list = []
        self.sessions: list[FakeSession] = []

    def __call__(self, cfg) -> FakeSession:
        session = FakeSession()
        self.configs.append(cfg)
        self.sessions.append(session)
        return session


@pytest.fixture
def speakers(monkeypatch):
    clients = FakeClients()
    monkeypatch.setattr(device, "APIClient", clients)
    monkeypatch.setattr(device, "ReconnectLogic", ConnectingReconnect)
    return clients


def live_hub(tmp_path, settings: Settings, streams, opened, loader=None) -> Hub:
    hub: Hub | None = None
    make_link = link_factory(lambda: hub.settings, streams, audio_url, opened, loader or (lambda path: b""))
    hub = Hub(make_store(tmp_path, settings), settings, DeviceRegistry(make_link))
    return hub


def answer(rid: str) -> list:
    return [
        ResponseCreated(rid),
        AudioDelta(rid, b"\x00\x00" * 2400),
        TranscriptDone(rid, "Так."),
        ResponseDone(rid, "completed", (), None, {}),
    ]


def types(client) -> list:
    return [event for event, _ in client.events]


async def start_turn(client, opened: Opened, sessions: int) -> FakeSession:
    assert await client.va_kwargs["handle_start"]("", 0, SETTINGS, None) == 0
    await wait_for(lambda: len(opened.sessions) >= sessions and opened.sessions[sessions - 1].calls[-1:] == ["clear"])
    return opened.sessions[sessions - 1]


async def test_speakers_connect_with_their_own_keys(tmp_path, speakers):
    hub = live_hub(tmp_path, Settings(devices=(KITCHEN, HALL, PORCH)), FakeStreams(), Opened())
    await hub.start()
    assert [(args, kw["noise_psk"], kw["expected_name"]) for args, kw, _ in speakers.made] == [
        (("192.0.2.10", 6053, None), key(1), "kitchen"),
        (("192.0.2.11", 6053, None), key(2), "hall"),
    ]
    assert [s["connected"] for s in hub.status()] == [True, True, False]
    await wait_for(lambda: hub.status()[0]["wake_words"]["active"] == ["okay_nabu"])
    speakers.last("192.0.2.10").state_cb(MediaPlayerEntityState(key=7, volume=0.78))
    assert [s["volume"] for s in hub.status()] == [78, 70, None]
    hub.set_volume("hall", 40)
    assert speakers.last("192.0.2.11").commands == [(7, {"volume": 0.4, "device_id": 0})]
    assert [s["volume"] for s in hub.status()] == [78, 40, None]
    await hub.close()


async def test_removing_a_speaker_mid_turn_stops_only_that_speaker(tmp_path, speakers):
    streams = FakeStreams()
    opened = Opened()
    hub = live_hub(tmp_path, Settings(devices=(KITCHEN, HALL), timing=FAST), streams, opened)
    await hub.start()
    kitchen, hall = speakers.last("192.0.2.10"), speakers.last("192.0.2.11")
    kitchen_session = await start_turn(kitchen, opened, 1)
    hall_session = await start_turn(hall, opened, 2)
    kitchen_session.push(SpeechStarted())
    hall_session.push(SpeechStarted())
    await wait_for(lambda: E.STT_VAD_START in types(kitchen) and E.STT_VAD_START in types(hall))
    kitchen_stream, hall_stream = streams.created

    await hub.update_settings({"devices": [{"name": "hall"}]})

    assert kitchen.disconnects == 1 and kitchen_session.closed and kitchen_stream.finished
    assert hall.disconnects == 0 and not hall_session.closed and not hall_stream.finished
    hall_session.push(SpeechStopped(), *answer("r1"))
    await wait_for(lambda: E.RUN_END in types(hall))
    assert types(hall)[-4:] == [E.INTENT_PROGRESS, E.INTENT_END, E.TTS_START, E.RUN_END]
    assert not hall_session.closed and [s["name"] for s in hub.status()] == ["hall"]
    await hub.close()
    assert hall_session.closed


async def test_adding_a_speaker_mid_turn_leaves_the_others_alone(tmp_path, speakers):
    opened = Opened()
    hub = live_hub(tmp_path, Settings(devices=(KITCHEN,), timing=FAST), FakeStreams(), opened)
    await hub.start()
    kitchen = speakers.last("192.0.2.10")
    link = hub.registry.link("kitchen")
    session = await start_turn(kitchen, opened, 1)
    session.push(SpeechStarted())
    await wait_for(lambda: E.STT_VAD_START in types(kitchen))

    await hub.update_settings({"devices": [{"name": "kitchen"}, {"name": "hall", "host": "192.0.2.11",
                                                                  "key": key(2)}]})

    assert hub.registry.link("kitchen") is link and kitchen.disconnects == 0 and not session.closed
    assert speakers.last("192.0.2.11").va_kwargs is not None
    session.push(SpeechStopped(), *answer("r1"))
    await wait_for(lambda: E.RUN_END in types(kitchen))
    assert E.ERROR not in types(kitchen) and len(opened.sessions) == 1
    await hub.close()


async def test_settings_change_mid_conversation_applies_from_the_next_turn(tmp_path, speakers):
    opened = Opened()
    settings = Settings(openai=OpenAISettings(api_key="sk-old", model="model-old"), devices=(KITCHEN,),
                        timing=replace(FAST, session_idle_s=5.0))
    hub = live_hub(tmp_path, settings, FakeStreams(), opened)
    await hub.start()
    kitchen = speakers.last("192.0.2.10")
    first = await start_turn(kitchen, opened, 1)
    first.push(SpeechStarted())
    await wait_for(lambda: E.STT_VAD_START in types(kitchen))

    await hub.update_settings({
        "openai": {"model": "model-new", "voice": "cedar", "api_key": "sk-new"},
        "assistant": {"language": "en"},
        "timing": {"first_turn_s": 0.15, "follow_up_s": 0.1, "session_idle_s": 5.0},
    })

    assert not first.closed and len(opened.sessions) == 1
    first.push(SpeechStopped(), *answer("r1"))
    await wait_for(lambda: E.RUN_END in types(kitchen))
    assert dict(kitchen.events[-3][1] or {}).get("continue_conversation") == "1"
    assert not first.closed and len(opened.sessions) == 1

    ends = types(kitchen).count(E.RUN_END)
    second = await start_turn(kitchen, opened, 2)
    assert second is not first and first.closed
    await wait_for(lambda: types(kitchen).count(E.RUN_END) > ends, timeout=1.0)
    old_cfg, new_cfg = opened.configs
    assert (old_cfg.model, old_cfg.voice, old_cfg.language, old_cfg.api_key) == ("model-old", "marin", "uk", "sk-old")
    assert (new_cfg.model, new_cfg.voice, new_cfg.language, new_cfg.api_key) == ("model-new", "cedar", "en", "sk-new")
    assert new_cfg.instructions != old_cfg.instructions
    await hub.close()


async def test_link_factory_wires_each_conversation(tmp_path, monkeypatch):
    made: list = []

    class RecordingConversation:
        def __init__(self, **kwargs) -> None:
            self.kwargs = kwargs

    class RecordingLink:
        def __init__(self, cfg, make_conversation) -> None:
            self.cfg = cfg
            self.conversation = make_conversation(self)
            made.append(self)

    monkeypatch.setattr(registry, "Conversation", RecordingConversation)
    monkeypatch.setattr(registry, "DeviceLink", RecordingLink)
    current = [Settings(openai=OpenAISettings(model="model-a"), timing=FAST)]
    opened = Opened()
    streams = FakeStreams()
    loaded: list[str] = []

    def loader(path: str) -> bytes:
        loaded.append(path)
        return b"\x01\x00"

    make_link = link_factory(lambda: current[0], streams, audio_url, opened, loader)
    plain = make_link(KITCHEN)
    debug = make_link(replace(HALL, debug_input_wav="/tmp/q.wav"))
    kw = plain.conversation.kwargs
    assert plain.cfg.name == "kitchen" and plain.cfg.encryption_key == key(1) and plain.cfg.host == "192.0.2.10"
    assert kw["device"] is plain and kw["streams"] is streams and kw["audio_url"] is audio_url
    assert kw["debug_input"] is None and debug.conversation.kwargs["debug_input"] == b"\x01\x00"
    assert loaded == ["/tmp/q.wav"]
    assert kw["timing"]() is FAST
    kw["session_factory"]()
    current[0] = Settings(openai=OpenAISettings(model="model-b"), timing=Timing())
    assert kw["timing"]() == Timing()
    kw["session_factory"]()
    assert [cfg.model for cfg in opened.configs] == ["model-a", "model-b"]
