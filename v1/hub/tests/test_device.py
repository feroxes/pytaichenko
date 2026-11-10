import asyncio
import logging
from dataclasses import replace

import pytest
from aioesphomeapi import (
    ZERO_NOISE_PSK,
    APIConnectionError,
    MediaPlayerEntityState,
    MediaPlayerInfo,
    SensorInfo,
    SensorState,
)
from aioesphomeapi.core import TimeoutAPIError

from device_fakes import PLAYER, FakeClient, FakeConversation, FakeReconnect
from fakes import E, wait_for
from voice_hub import device
from voice_hub.config import DeviceConfig
from voice_hub.device import DeviceLink, VolumeError, WakeWordError, WakeWords, make_client

DEV_CFG = DeviceConfig(name="kitchen", host="192.0.2.10", encryption_key="not-a-real-key", port=6054)


def make(client=None, **kw):
    client = client if client is not None else FakeClient([PLAYER])
    made: list[FakeConversation] = []

    def make_conv(link):
        conv = FakeConversation(link, client.trace)
        made.append(conv)
        return conv

    link = DeviceLink(DEV_CFG, make_conv, client=client, **kw)
    return link, client, made[0]


async def test_on_connect_subscribes_voice_and_finds_media_player():
    link, client, conv = make(FakeClient([PLAYER]))
    await link.on_connect()
    assert link.connected and client.va_kwargs["handle_audio"] is not None
    assert client.va_kwargs["handle_start"] == conv.handle_start
    assert client.va_kwargs["handle_stop"] == conv.handle_stop
    assert client.va_kwargs["handle_audio"] == conv.handle_audio
    assert client.state_cb is not None


def test_device_link_starts_with_default_volume_and_name():
    link, _, conv = make()
    assert link.name == "kitchen" and link.volume == 0.7 and not link.connected
    assert conv.device is link


async def test_picks_first_media_player_among_entities():
    other = SensorInfo(object_id="heap", key=3, name="Heap free")
    second = MediaPlayerInfo(object_id="second", key=9, name="")
    link, client, _ = make(FakeClient([other, PLAYER, second]))
    await link.on_connect()
    link.set_volume(0.2)
    assert client.commands == [(7, {"volume": 0.2, "device_id": 0})]


async def test_set_volume_sends_command():
    link, client, _ = make()
    await link.on_connect()
    link.set_volume(0.55)
    assert client.commands == [(7, {"volume": 0.55, "device_id": 0})]


async def test_set_volume_uses_device_id_of_media_player():
    player = MediaPlayerInfo(object_id="kitchen", key=7, name="", device_id=42)
    link, client, _ = make(FakeClient([player]))
    await link.on_connect()
    link.set_volume(0.3)
    assert client.commands == [(7, {"volume": 0.3, "device_id": 42})]


async def test_volume_tracks_state():
    link, client, _ = make()
    await link.on_connect()
    client.state_cb(MediaPlayerEntityState(key=7, volume=0.4))
    assert link.volume == 0.4


async def test_state_of_other_entities_is_ignored():
    link, client, _ = make()
    await link.on_connect()
    client.state_cb(MediaPlayerEntityState(key=8, volume=0.1))
    client.state_cb(SensorState(key=7, state=0.1))
    assert link.volume == 0.7


async def test_without_media_player_volume_is_untouched():
    link, client, _ = make(FakeClient([SensorInfo(object_id="heap", key=3, name="Heap free")]))
    await link.on_connect()
    assert link.connected
    link.set_volume(0.5)
    client.state_cb(MediaPlayerEntityState(key=7, volume=0.1))
    assert client.commands == [] and link.volume == 0.7


async def test_set_volume_ignored_when_disconnected():
    link, client, _ = make()
    link.set_volume(0.5)
    await link.on_connect()
    await link.on_disconnect(False)
    link.set_volume(0.5)
    assert client.commands == []


async def test_disconnect_drops_subscription_without_unsub():
    link, client, conv = make()
    await link.on_connect()
    await link.on_disconnect(False)
    assert client.unsub_calls == 0 and not link.connected and conv.closed
    await link.on_connect()
    assert client.subscribe_calls == 2 and client.unsub_calls == 0 and link.connected


async def test_volume_survives_disconnect():
    link, client, _ = make()
    await link.on_connect()
    client.state_cb(MediaPlayerEntityState(key=7, volume=0.4))
    await link.on_disconnect(True)
    assert link.volume == 0.4


async def test_player_volume_is_known_only_while_connected_with_a_media_player():
    link, client, _ = make()
    assert link.player_volume is None
    await link.on_connect()
    client.state_cb(MediaPlayerEntityState(key=7, volume=0.4))
    assert link.player_volume == 0.4
    await link.on_disconnect(False)
    assert link.player_volume is None and link.volume == 0.4
    bare, _, _ = make(FakeClient([SensorInfo(object_id="heap", key=3, name="Heap free")]))
    await bare.on_connect()
    assert bare.connected and bare.player_volume is None


async def test_change_volume_sends_the_level_and_shows_it_at_once():
    link, client, _ = make()
    await link.on_connect()
    link.change_volume(0.35)
    assert client.commands == [(7, {"volume": 0.35, "device_id": 0})]
    assert link.volume == 0.35 and link.player_volume == 0.35


async def test_change_volume_refuses_a_speaker_that_is_offline_or_has_no_media_player():
    link, client, _ = make()
    with pytest.raises(VolumeError) as offline:
        link.change_volume(0.5)
    bare, bare_client, _ = make(FakeClient([SensorInfo(object_id="heap", key=3, name="Heap free")]))
    await bare.on_connect()
    with pytest.raises(VolumeError) as no_player:
        bare.change_volume(0.5)
    assert offline.value.code == no_player.value.code == "offline"
    assert client.commands == [] and bare_client.commands == [] and link.volume == bare.volume == 0.7


async def test_change_volume_reports_a_dead_connection(caplog):
    link, client, _ = make()
    await link.on_connect()

    def broken(key, **kwargs) -> None:
        raise APIConnectionError("Connection is closed")

    client.media_player_command = broken
    with pytest.raises(VolumeError) as failed:
        link.change_volume(0.5)
    assert failed.value.code == "failed" and link.volume == 0.7 and "kitchen" in caplog.text


async def test_set_volume_from_the_conversation_is_shown_at_once():
    link, client, _ = make()
    await link.on_connect()
    link.set_volume(0.55)
    assert link.player_volume == 0.55


async def test_send_event_forwards_when_connected():
    link, client, _ = make()
    await link.on_connect()
    link.send_event(E.RUN_START, {"url": "http://hub/a.wav"})
    link.send_event(E.RUN_END)
    assert client.events == [(E.RUN_START, {"url": "http://hub/a.wav"}), (E.RUN_END, None)]


def test_send_event_ignored_when_disconnected():
    link, client, _ = make()
    link.send_event(E.RUN_START, {"url": "x"})
    assert client.events == []


async def test_send_event_stops_after_disconnect():
    link, client, _ = make()
    await link.on_connect()
    await link.on_disconnect(False)
    link.send_event(E.RUN_END)
    assert client.events == []


async def test_send_event_survives_a_dead_connection(caplog):
    link, client, _ = make()
    await link.on_connect()
    client.fail_send = APIConnectionError("Connection is closed")
    link.send_event(E.RUN_END)
    assert client.events == [] and "kitchen" in caplog.text


async def test_set_volume_survives_a_dead_connection(caplog):
    link, client, _ = make()
    await link.on_connect()

    def broken(key, **kwargs) -> None:
        raise APIConnectionError("Connection is closed")

    client.media_player_command = broken
    link.set_volume(0.5)
    assert "kitchen" in caplog.text


async def test_failed_setup_drops_the_connection(caplog):
    client = FakeClient([PLAYER])
    client.fail_list = APIConnectionError("lost")
    link, _, _ = make(client)
    await link.on_connect()
    assert not link.connected and client.disconnects == 1 and client.va_kwargs is None
    assert "kitchen" in caplog.text


async def test_disconnect_does_not_wait_for_a_stuck_close(monkeypatch):
    monkeypatch.setattr(device, "CLOSE_WAIT_S", 0.05)
    link, _, conv = make()
    conv.gate = asyncio.Event()
    await link.on_connect()
    await asyncio.wait_for(link.on_disconnect(False), 1.0)
    assert not link.connected and conv.close_calls == 1 and not conv.closed
    conv.gate.set()
    await wait_for(lambda: conv.closed)


async def test_stop_waits_for_a_stuck_close(monkeypatch):
    monkeypatch.setattr(device, "CLOSE_WAIT_S", 0.05)
    link, _, conv = make()
    conv.gate = asyncio.Event()
    await link.on_connect()
    await link.on_disconnect(False)
    asyncio.get_running_loop().call_later(0.1, conv.gate.set)
    await asyncio.wait_for(link.stop(), 1.0)
    assert conv.closed


async def test_start_builds_reconnect_logic_inside_the_loop(monkeypatch):
    FakeReconnect.instances = []
    monkeypatch.setattr(device, "ReconnectLogic", FakeReconnect)
    link, client, _ = make()
    await link.start()
    (rl,) = FakeReconnect.instances
    assert rl.kwargs["client"] is client and rl.kwargs["name"] == "kitchen"
    assert rl.kwargs["on_connect"] == link.on_connect and rl.kwargs["on_disconnect"] == link.on_disconnect
    assert rl.running_loop is asyncio.get_running_loop() and client.trace == ["reconnect.start"]


async def test_second_start_does_nothing(monkeypatch):
    FakeReconnect.instances = []
    monkeypatch.setattr(device, "ReconnectLogic", FakeReconnect)
    link, client, _ = make()
    await link.start()
    await link.start()
    assert len(FakeReconnect.instances) == 1 and client.trace == ["reconnect.start"]


async def test_stop_stops_reconnects_then_disconnects_then_closes(monkeypatch):
    FakeReconnect.instances = []
    monkeypatch.setattr(device, "ReconnectLogic", FakeReconnect)
    link, client, conv = make()
    await link.start()
    await link.on_connect()
    client.trace.clear()
    await link.stop()
    assert client.trace == ["reconnect.stop", "client.disconnect", "conversation.close"]
    assert conv.closed


async def test_stop_without_start_still_closes_everything():
    link, client, conv = make()
    await link.stop()
    assert client.disconnects == 1 and conv.closed


async def test_stop_closes_conversation_even_if_disconnect_fails():
    link, client, conv = make()

    async def broken(force: bool = False) -> None:
        raise OSError("boom")

    client.disconnect = broken
    with pytest.raises(OSError):
        await link.stop()
    assert conv.closed


def test_default_client_is_built_from_config(monkeypatch):
    built = []

    class SpyClient(FakeClient):
        def __init__(self, *args, **kwargs) -> None:
            super().__init__()
            built.append((args, kwargs))

    monkeypatch.setattr(device, "APIClient", SpyClient)
    link = DeviceLink(DEV_CFG, lambda link: FakeConversation(link))
    assert built == [
        (
            ("192.0.2.10", 6054, None),
            {"noise_psk": "not-a-real-key", "client_info": "voice-hub", "expected_name": "kitchen"},
        )
    ]
    assert link.name == "kitchen"


def test_make_client_without_a_name_skips_the_name_check(monkeypatch):
    built = []
    monkeypatch.setattr(device, "APIClient", lambda *args, **kwargs: built.append((args, kwargs)) or "client")
    assert make_client("192.0.2.20", 6053, ZERO_NOISE_PSK) == "client"
    assert built == [
        (("192.0.2.20", 6053, None), {"noise_psk": ZERO_NOISE_PSK, "client_info": "voice-hub", "expected_name": None})
    ]


AVAILABLE = (("okay_nabu", "Okay Nabu"), ("hey_mycroft", "Hey Mycroft"))


def test_wake_words_are_empty_before_connecting():
    link, _, _ = make()
    assert link.wake_words == WakeWords() and link.wake_words.view() == {"available": [], "active": []}


async def connected(link) -> None:
    await link.on_connect()
    await wait_for(lambda: link.wake_words != WakeWords())


async def test_wake_words_are_read_after_subscribing():
    link, client, _ = make()
    await connected(link)
    assert client.order == ["subscribe", "get_config"] and client.config_timeouts == [device.WAKE_WORDS_TIMEOUT_S]
    assert link.wake_words == WakeWords(available=AVAILABLE, active=("okay_nabu",))
    assert link.wake_words.view() == {"available": list(AVAILABLE), "active": ["okay_nabu"]}


async def test_unreadable_wake_words_keep_the_connection(caplog):
    client = FakeClient([PLAYER])
    client.fail_config = TimeoutAPIError("no answer")
    link, _, _ = make(client)
    await link.on_connect()
    await wait_for(lambda: "wake word" in caplog.text)
    assert link.connected and link.wake_words == WakeWords() and client.disconnects == 0
    assert "kitchen" in caplog.text


async def test_wake_words_are_forgotten_on_disconnect():
    link, _, _ = make()
    await connected(link)
    await link.on_disconnect(False)
    assert link.wake_words == WakeWords()


async def test_connecting_does_not_wait_for_the_wake_words():
    client = FakeClient([PLAYER])
    client.config_gate = asyncio.Event()
    link, _, _ = make(client)
    await asyncio.wait_for(link.on_connect(), 0.5)
    assert link.connected and link.wake_words == WakeWords()
    await wait_for(lambda: client.order == ["subscribe", "get_config"])
    client.config_gate.set()
    await wait_for(lambda: link.wake_words.active == ("okay_nabu",))


async def test_disconnect_cancels_a_slow_wake_word_read():
    client = FakeClient([PLAYER])
    client.config_gate = asyncio.Event()
    link, _, _ = make(client)
    await asyncio.wait_for(link.on_connect(), 0.5)
    await wait_for(lambda: client.order == ["subscribe", "get_config"])
    await link.on_disconnect(False)
    await wait_for(lambda: client.config_cancelled == 1)
    client.config_gate.set()
    await asyncio.sleep(0.02)
    assert link.wake_words == WakeWords()


async def test_reconnect_reads_the_wake_words_again():
    link, client, _ = make()
    await connected(link)
    await link.on_disconnect(False)
    client.wake_config = replace(client.wake_config, active_wake_words=["hey_mycroft"])
    await link.on_connect()
    await wait_for(lambda: link.wake_words.active == ("hey_mycroft",))


async def test_stop_during_a_slow_wake_word_read_returns_promptly(monkeypatch):
    monkeypatch.setattr(device, "ReconnectLogic", FakeReconnect)
    client = FakeClient([PLAYER])
    client.config_gate = asyncio.Event()
    link, _, conv = make(client)
    await link.start()
    await asyncio.wait_for(link.on_connect(), 0.5)
    await wait_for(lambda: client.order == ["subscribe", "get_config"])
    await asyncio.wait_for(link.stop(), 0.5)
    assert conv.closed and client.disconnects == 1 and client.config_cancelled == 1
    client.config_gate.set()
    await asyncio.sleep(0.02)
    assert link.wake_words == WakeWords()


async def test_failed_setup_reads_no_wake_words():
    client = FakeClient([PLAYER])
    client.fail_list = APIConnectionError("lost")
    link, _, _ = make(client)
    await link.on_connect()
    await asyncio.sleep(0.02)
    assert "get_config" not in client.order and link.wake_words == WakeWords()


async def test_set_wake_word_sends_the_choice_and_reads_it_back(caplog):
    caplog.set_level(logging.INFO)
    link, client, _ = make()
    await connected(link)
    await link.set_wake_word("hey_mycroft")
    assert client.set_configs == [["hey_mycroft"]]
    assert link.wake_words == WakeWords(available=AVAILABLE, active=("hey_mycroft",))
    assert client.order == ["subscribe", "get_config", "get_config"]
    assert "hey_mycroft" in caplog.text


async def test_set_wake_word_needs_a_connection():
    link, client, _ = make()
    with pytest.raises(WakeWordError) as err:
        await link.set_wake_word("okay_nabu")
    assert err.value.code == "offline" and "kitchen" in str(err.value)
    await link.on_connect()
    await link.on_disconnect(False)
    with pytest.raises(WakeWordError):
        await link.set_wake_word("okay_nabu")
    assert client.set_configs == []


async def test_set_wake_word_rejects_an_unknown_id():
    link, client, _ = make()
    await connected(link)
    with pytest.raises(WakeWordError) as err:
        await link.set_wake_word("hey_nobody")
    assert err.value.code == "unknown" and "hey_nobody" in str(err.value)
    assert client.set_configs == [] and link.wake_words.active == ("okay_nabu",)


@pytest.mark.parametrize("where", ["set", "read back"])
async def test_set_wake_word_reports_a_dead_connection(where, caplog):
    link, client, _ = make()
    await connected(link)
    if where == "set":
        client.fail_set_config = APIConnectionError("Connection is closed")
    else:
        client.fail_config = APIConnectionError("Connection is closed")
    with pytest.raises(WakeWordError) as err:
        await link.set_wake_word("hey_mycroft")
    assert err.value.code == "failed" and "kitchen" in str(err.value) and "closed" not in str(err.value)
    assert link.wake_words.active == ("okay_nabu",) and "Connection is closed" in caplog.text
