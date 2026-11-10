import asyncio
import base64
import logging

import pytest
from aioesphomeapi import (
    ZERO_NOISE_PSK,
    APIConnectionError,
    DeviceInfo,
    EncryptionPlaintextAPIError,
    InvalidEncryptionKeyAPIError,
)
from aioesphomeapi.core import TimeoutAPIError

from device_fakes import AdoptClient, AdoptFactory, gone_now, keyed_now, keyless_now
from voice_hub import adopt as adopt_module
from voice_hub.adopt import AdoptError, Adopted, adopt, identify
from voice_hub.settings import SettingsError

SWITCH_PAUSE_S = adopt_module.PROBE_DELAY_S


@pytest.fixture(autouse=True)
def quick(monkeypatch):
    monkeypatch.setattr(adopt_module, "PROBE_DELAY_S", 0.0)


class Keep:
    def __init__(self, client: AdoptClient | None = None, fail: Exception | None = None) -> None:
        self.kept: list[Adopted] = []
        self.client = client
        self.fail = fail

    def __call__(self, adopted: Adopted) -> None:
        if self.client is not None:
            self.client.calls.append("keep")
        if self.fail is not None:
            raise self.fail
        self.kept.append(adopted)


async def run(client: AdoptClient, *probes: AdoptClient, keep: Keep | None = None):
    factory = AdoptFactory(client, *probes)
    keep = keep or Keep(client)
    return await adopt("192.0.2.30", 6053, factory, keep), factory, keep


async def test_adopt_keeps_the_key_then_gives_it_to_the_speaker():
    client = AdoptClient()
    result, factory, keep = await run(client)
    assert factory.args == [("192.0.2.30", 6053, ZERO_NOISE_PSK)]
    assert client.calls == ["connect", "device_info", "keep", "set_key", "disconnect"]
    assert keep.kept == [result] and result.name == "speaker-a1b2c3"
    assert len(base64.b64decode(result.key, validate=True)) == 32
    assert client.keys == [result.key.encode("ascii")]


async def test_every_adoption_makes_a_different_key():
    keys = {(await run(AdoptClient()))[0].key for _ in range(5)}
    assert len(keys) == 5 and base64.b64encode(bytes(32)).decode() not in keys


async def test_failed_keep_sends_no_key():
    client = AdoptClient()
    with pytest.raises(SettingsError):
        await run(client, keep=Keep(client, SettingsError("settings.json: cannot be saved (No space left on device)")))
    assert client.keys == [] and client.calls == ["connect", "device_info", "keep", "disconnect"]


async def test_speaker_with_a_key_is_refused_and_keeps_it():
    client = AdoptClient(DeviceInfo(name="speaker-a1b2c3", api_encryption_provisionable=False))
    with pytest.raises(AdoptError) as err:
        await run(client)
    assert err.value.code == "has_key" and "already has a key" in str(err.value)
    assert client.calls == ["connect", "device_info", "disconnect"] and client.keys == []


async def test_speaker_that_rejects_the_zero_key_already_has_one():
    with pytest.raises(AdoptError) as err:
        await run(keyed_now())
    assert err.value.code == "has_key" and "already has a key" in str(err.value)


async def test_speaker_without_encryption_cannot_be_adopted():
    client = AdoptClient()
    client.fail_connect = EncryptionPlaintextAPIError("plaintext")
    with pytest.raises(AdoptError) as err:
        await run(client)
    assert err.value.code == "unsupported" and client.keys == []


@pytest.mark.parametrize("error", [APIConnectionError("refused"), OSError("no route"), TimeoutAPIError("slow")])
async def test_unreachable_speaker(error):
    client = AdoptClient()
    client.fail_connect = error
    with pytest.raises(AdoptError) as err:
        await run(client)
    assert err.value.code == "unreachable" and "192.0.2.30" in str(err.value)
    assert client.keys == [] and client.calls[-1] == "disconnect" and "keep" not in client.calls


async def test_speaker_that_does_not_answer_times_out(monkeypatch):
    monkeypatch.setattr(adopt_module, "CONNECT_TIMEOUT_S", 0.05)
    client = AdoptClient()
    client.hang_connect = True
    with pytest.raises(AdoptError) as err:
        await asyncio.wait_for(run(client), 2.0)
    assert err.value.code == "unreachable" and client.keys == []


async def test_speaker_without_a_name_is_not_given_a_key():
    client = AdoptClient(DeviceInfo(name="  ", api_encryption_provisionable=True))
    with pytest.raises(AdoptError) as err:
        await run(client)
    assert err.value.code == "no_name" and client.keys == [] and "keep" not in client.calls


async def test_speaker_that_refuses_the_key():
    client = AdoptClient()
    client.accept = False
    with pytest.raises(AdoptError) as err:
        await run(client)
    assert err.value.code == "refused" and client.calls[-1] == "disconnect"


async def test_lost_answer_and_still_keyless_means_refused():
    client = AdoptClient()
    client.fail_set = APIConnectionError("Connection is closed")
    probe = keyless_now()
    with pytest.raises(AdoptError) as err:
        await run(client, probe)
    assert err.value.code == "refused"
    assert client.calls == ["connect", "device_info", "keep", "set_key", "disconnect"]
    assert probe.calls == ["connect", "device_info", "disconnect"] and probe.keys == []


async def test_lost_answer_and_zero_key_rejected_means_adopted():
    client = AdoptClient()
    client.fail_set = APIConnectionError("Connection is closed")
    probe = keyed_now()
    result, factory, keep = await run(client, probe)
    assert keep.kept == [result] and factory.args[1] == ("192.0.2.30", 6053, ZERO_NOISE_PSK)
    assert probe.calls == ["connect", "disconnect"]


async def test_lost_answer_and_speaker_says_it_has_a_key_means_adopted():
    client = AdoptClient()
    client.fail_set = APIConnectionError("Connection is closed")
    probe = AdoptClient(DeviceInfo(name="speaker-a1b2c3", api_encryption_provisionable=False))
    result, _, keep = await run(client, probe)
    assert keep.kept == [result]


async def test_lost_answer_and_unreachable_speaker_keeps_the_key(caplog):
    client = AdoptClient()
    client.fail_set = APIConnectionError("Connection is closed")
    result, _, keep = await run(client, gone_now())
    assert keep.kept == [result] and "192.0.2.30" in caplog.text and result.key not in caplog.text


async def test_slow_answer_is_checked_again(monkeypatch):
    monkeypatch.setattr(adopt_module, "SET_KEY_TIMEOUT_S", 0.05)
    client = AdoptClient()
    client.hang_set = True
    with pytest.raises(AdoptError) as err:
        await asyncio.wait_for(run(client, keyless_now()), 2.0)
    assert err.value.code == "refused" and client.calls[-1] == "disconnect"


async def test_probe_waits_for_the_key_to_take_effect(monkeypatch):
    monkeypatch.setattr(adopt_module, "PROBE_DELAY_S", 0.2)
    client = AdoptClient()
    client.fail_set = APIConnectionError("Connection is closed")
    loop = asyncio.get_running_loop()
    start = loop.time()
    await run(client, keyed_now())
    assert loop.time() - start >= 0.2


async def test_confirmed_key_is_given_time_to_take_effect_before_returning(monkeypatch):
    monkeypatch.setattr(adopt_module, "PROBE_DELAY_S", 0.2)
    loop = asyncio.get_running_loop()
    client = AdoptClient()
    dropped: list[float] = []

    async def disconnect(force: bool = False) -> None:
        client.calls.append("disconnect")
        dropped.append(loop.time())

    client.disconnect = disconnect
    result, factory, keep = await run(client)
    assert keep.kept == [result] and len(factory.args) == 1
    assert client.calls == ["connect", "device_info", "keep", "set_key", "disconnect"]
    assert loop.time() - dropped[0] >= 0.2


def test_the_pause_after_a_new_key_outlasts_the_speaker_switch():
    assert SWITCH_PAUSE_S >= 0.2


async def test_the_whole_adoption_is_bounded_in_time(monkeypatch):
    monkeypatch.setattr(adopt_module, "SET_KEY_TIMEOUT_S", 0.05)
    monkeypatch.setattr(adopt_module, "CONNECT_TIMEOUT_S", 0.05)
    client = AdoptClient()
    client.hang_set = True
    probe = AdoptClient()
    probe.hang_connect = True
    result, _, keep = await asyncio.wait_for(run(client, probe), 2.0)
    assert keep.kept == [result]


async def test_failed_disconnect_does_not_hide_the_result():
    client = AdoptClient()

    async def broken(force: bool = False) -> None:
        raise APIConnectionError("already closed")

    client.disconnect = broken
    result, _, _ = await run(client)
    assert result.name == "speaker-a1b2c3"


async def test_the_key_is_never_shown(caplog):
    caplog.set_level(logging.DEBUG)
    result, _, _ = await run(AdoptClient())
    assert result.key not in repr(result) and result.key not in caplog.text
    assert "speaker-a1b2c3" in caplog.text
    for failure in (False, APIConnectionError("x")):
        client = AdoptClient()
        if failure is False:
            client.accept = False
        else:
            client.fail_set = failure
        with pytest.raises(AdoptError) as err:
            await run(client, keyless_now())
        sent = client.keys[0].decode()
        assert sent not in str(err.value) and sent not in caplog.text


def test_adopted_is_frozen():
    result = Adopted(name="speaker", key="k")
    with pytest.raises(AttributeError):
        result.name = "other"


OWN_KEY = base64.b64encode(bytes([9]) * 32).decode("ascii")


async def test_identify_reads_the_name_over_the_given_key():
    client = AdoptClient(DeviceInfo(name="speaker-d4e5f6", api_encryption_provisionable=False))
    factory = AdoptFactory(client)
    assert await identify("192.0.2.31", 6054, OWN_KEY, factory) == "speaker-d4e5f6"
    assert factory.args == [("192.0.2.31", 6054, OWN_KEY)]
    assert client.calls == ["connect", "device_info", "disconnect"] and client.keys == []


@pytest.mark.parametrize("error, code", [
    (InvalidEncryptionKeyAPIError("Invalid encryption key", "speaker-d4e5f6"), "wrong_key"),
    (EncryptionPlaintextAPIError("plaintext"), "unsupported"),
    (APIConnectionError("refused"), "unreachable"),
    (OSError("no route"), "unreachable"),
    (TimeoutAPIError("slow"), "unreachable"),
])
async def test_identify_explains_why_it_failed(error, code, caplog):
    caplog.set_level(logging.DEBUG)
    client = AdoptClient()
    client.fail_connect = error
    with pytest.raises(AdoptError) as err:
        await identify("192.0.2.31", 6053, OWN_KEY, AdoptFactory(client))
    assert err.value.code == code and "192.0.2.31" in str(err.value)
    assert OWN_KEY not in str(err.value) and OWN_KEY not in caplog.text
    assert client.calls[-1] == "disconnect" and client.keys == []


async def test_identify_needs_a_name():
    client = AdoptClient(DeviceInfo(name=" ", api_encryption_provisionable=False))
    with pytest.raises(AdoptError) as err:
        await identify("192.0.2.31", 6053, OWN_KEY, AdoptFactory(client))
    assert err.value.code == "no_name" and client.calls[-1] == "disconnect"


async def test_identify_gives_up_on_a_silent_speaker(monkeypatch):
    monkeypatch.setattr(adopt_module, "CONNECT_TIMEOUT_S", 0.05)
    client = AdoptClient()
    client.hang_connect = True
    with pytest.raises(AdoptError) as err:
        await asyncio.wait_for(identify("192.0.2.31", 6053, OWN_KEY, AdoptFactory(client)), 2.0)
    assert err.value.code == "unreachable"


def wrong_key_client() -> AdoptClient:
    client = AdoptClient()
    client.fail_connect = InvalidEncryptionKeyAPIError("Invalid encryption key", "speaker-a1b2c3")
    return client


async def test_identify_says_when_a_speaker_has_no_key_yet():
    first = wrong_key_client()
    factory = AdoptFactory(first, keyless_now())
    with pytest.raises(AdoptError) as err:
        await identify("192.0.2.31", 6053, OWN_KEY, factory)
    assert err.value.code == "no_key_yet" and "without a key" in str(err.value)
    assert factory.args == [("192.0.2.31", 6053, OWN_KEY), ("192.0.2.31", 6053, ZERO_NOISE_PSK)]
    assert first.calls == ["connect", "disconnect"] and factory.clients[1].calls[-1] == "disconnect"
    assert all(client.keys == [] for client in factory.clients)


@pytest.mark.parametrize("probe", [
    lambda: AdoptClient(DeviceInfo(name="speaker-a1b2c3", api_encryption_provisionable=False)),
    keyed_now,
    gone_now,
])
async def test_identify_reports_a_wrong_key_when_the_speaker_has_one(probe):
    factory = AdoptFactory(wrong_key_client(), probe())
    with pytest.raises(AdoptError) as err:
        await identify("192.0.2.31", 6053, OWN_KEY, factory)
    assert err.value.code == "wrong_key" and len(factory.args) == 2
