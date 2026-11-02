import asyncio
import logging

import pytest
from zeroconf import ServiceStateChange

from fakes import wait_for
from voice_hub.discovery import SERVICE_TYPE, Discovery, Found, parse_service

NAME = f"speaker-a1b2c3.{SERVICE_TYPE}"
TXT = {
    b"friendly_name": "Кухня".encode(),
    b"version": b"2026.10.0",
    b"mac": b"a1b2c3d4e5f6",
    b"platform": b"ESP32",
    b"network": b"wifi",
    b"api_encryption_supported": b"Noise_NNpsk0_25519_ChaChaPoly_SHA256",
    b"api_provisioning": b"zero-psk",
}
KEYED_TXT = {
    b"friendly_name": b"Hall",
    b"api_encryption": b"Noise_NNpsk0_25519_ChaChaPoly_SHA256",
}


def test_service_waiting_for_a_key_is_provisionable():
    found = parse_service(NAME, ["192.0.2.40"], 6053, TXT)
    assert found == Found(name="speaker-a1b2c3", host="192.0.2.40", port=6053, provisionable=True,
                          friendly_name="Кухня")


def test_service_with_a_key_is_not_provisionable():
    found = parse_service(f"hall.{SERVICE_TYPE}", ["192.0.2.41"], 6053, KEYED_TXT)
    assert found == Found(name="hall", host="192.0.2.41", port=6053, provisionable=False, friendly_name="Hall")


@pytest.mark.parametrize("value", [b"", b"zero", b"psk", None])
def test_only_zero_psk_means_provisionable(value):
    assert parse_service(NAME, ["192.0.2.40"], 6053, TXT | {b"api_provisioning": value}).provisionable is False


def test_ipv4_address_is_preferred():
    found = parse_service(NAME, ["2001:db8::1", "192.0.2.40"], 6053, TXT)
    assert found.host == "192.0.2.40"


def test_ipv6_address_is_used_when_there_is_no_ipv4():
    assert parse_service(NAME, ["2001:db8::1"], 6053, TXT).host == "2001:db8::1"


def test_missing_friendly_name_falls_back_to_the_name():
    assert parse_service(NAME, ["192.0.2.40"], 6053, {b"api_provisioning": b"zero-psk"}).friendly_name == (
        "speaker-a1b2c3"
    )


def test_damaged_text_does_not_break_parsing():
    found = parse_service(NAME, ["192.0.2.40"], 6053, {b"friendly_name": b"\xff\xfe", b"\xff": b"x"})
    assert found is not None and found.provisionable is False and found.friendly_name


@pytest.mark.parametrize(
    "args",
    [
        ("speaker._http._tcp.local.", ["192.0.2.40"], 6053),
        (SERVICE_TYPE, ["192.0.2.40"], 6053),
        (NAME, [], 6053),
        (NAME, ["not-an-address"], 6053),
        (NAME, ["192.0.2.40"], None),
        (NAME, ["192.0.2.40"], 0),
    ],
)
def test_incomplete_services_are_skipped(args):
    assert parse_service(*args, TXT) is None


class FakeZeroconf:
    def __init__(self) -> None:
        self.zeroconf = object()
        self.closed = False

    async def async_close(self) -> None:
        self.closed = True


class FakeBrowser:
    def __init__(self, zeroconf, type_, handlers=None) -> None:
        self.zeroconf = zeroconf
        self.type_ = type_
        self.handlers = handlers
        self.cancelled = False

    async def async_cancel(self) -> None:
        self.cancelled = True

    def fire(self, name: str, change: ServiceStateChange) -> None:
        for handler in self.handlers:
            handler(zeroconf=self.zeroconf, service_type=self.type_, name=name, state_change=change)


class FakeInfo:
    services: dict[str, tuple[list[str], int, dict]] = {}
    gate: asyncio.Event | None = None
    fail: Exception | None = None
    requests: list = []

    def __init__(self, type_: str, name: str) -> None:
        self.type = type_
        self.name = name
        self.port: int | None = None
        self.properties: dict = {}
        self._addresses: list[str] = []

    async def async_request(self, zc, timeout: float) -> bool:
        FakeInfo.requests.append((zc, self.name, timeout))
        if FakeInfo.gate is not None:
            await FakeInfo.gate.wait()
        if FakeInfo.fail is not None:
            raise FakeInfo.fail
        if self.name not in FakeInfo.services:
            return False
        self._addresses, self.port, self.properties = FakeInfo.services[self.name]
        return True

    def parsed_addresses(self) -> list[str]:
        return list(self._addresses)


@pytest.fixture
def parts():
    FakeInfo.services, FakeInfo.gate, FakeInfo.fail, FakeInfo.requests = {}, None, None, []
    made: dict = {"zc": [], "browsers": []}

    def zc_factory() -> FakeZeroconf:
        zc = FakeZeroconf()
        made["zc"].append(zc)
        return zc

    def browser_factory(zeroconf, type_, handlers=None) -> FakeBrowser:
        browser = FakeBrowser(zeroconf, type_, handlers)
        made["browsers"].append(browser)
        return browser

    made["discovery"] = Discovery(zc_factory, browser_factory, FakeInfo)
    return made


async def test_discovery_browses_esphome_speakers(parts):
    discovery = parts["discovery"]
    await discovery.start()
    (zc,), (browser,) = parts["zc"], parts["browsers"]
    assert browser.zeroconf is zc.zeroconf and browser.type_ == SERVICE_TYPE and len(browser.handlers) == 1
    assert discovery.snapshot() == []
    await discovery.stop()
    assert browser.cancelled and zc.closed


async def test_found_speakers_appear_change_and_go(parts):
    discovery = parts["discovery"]
    await discovery.start()
    (browser,) = parts["browsers"]
    FakeInfo.services[NAME] = (["192.0.2.40"], 6053, TXT)
    browser.fire(NAME, ServiceStateChange.Added)
    await wait_for(lambda: len(discovery.snapshot()) == 1)
    assert discovery.snapshot()[0].provisionable is True
    assert FakeInfo.requests[0][1] == NAME and FakeInfo.requests[0][2] > 0
    FakeInfo.services[NAME] = (["192.0.2.42"], 6053, KEYED_TXT)
    browser.fire(NAME, ServiceStateChange.Updated)
    await wait_for(lambda: discovery.snapshot()[0].host == "192.0.2.42")
    assert discovery.snapshot()[0].provisionable is False
    browser.fire(NAME, ServiceStateChange.Removed)
    assert discovery.snapshot() == []
    await discovery.stop()


async def test_snapshot_is_sorted_by_name(parts):
    discovery = parts["discovery"]
    await discovery.start()
    (browser,) = parts["browsers"]
    for name, host in (("porch", "192.0.2.43"), ("hall", "192.0.2.41"), ("kitchen", "192.0.2.40")):
        FakeInfo.services[f"{name}.{SERVICE_TYPE}"] = ([host], 6053, KEYED_TXT)
        browser.fire(f"{name}.{SERVICE_TYPE}", ServiceStateChange.Added)
    await wait_for(lambda: len(discovery.snapshot()) == 3)
    assert [f.name for f in discovery.snapshot()] == ["hall", "kitchen", "porch"]
    await discovery.stop()


async def test_services_that_cannot_be_resolved_are_skipped(parts):
    discovery = parts["discovery"]
    await discovery.start()
    (browser,) = parts["browsers"]
    browser.fire(NAME, ServiceStateChange.Added)
    await wait_for(lambda: len(FakeInfo.requests) == 1)
    await asyncio.sleep(0.01)
    FakeInfo.services[f"bad.{SERVICE_TYPE}"] = ([], 6053, TXT)
    browser.fire(f"bad.{SERVICE_TYPE}", ServiceStateChange.Added)
    await wait_for(lambda: len(FakeInfo.requests) == 2)
    await asyncio.sleep(0.01)
    FakeInfo.fail = OSError("network down")
    browser.fire(f"other.{SERVICE_TYPE}", ServiceStateChange.Added)
    await wait_for(lambda: len(FakeInfo.requests) == 3)
    await asyncio.sleep(0.01)
    assert discovery.snapshot() == []
    await discovery.stop()


async def test_removed_while_looking_up_stays_removed(parts):
    discovery = parts["discovery"]
    await discovery.start()
    (browser,) = parts["browsers"]
    FakeInfo.services[NAME] = (["192.0.2.40"], 6053, TXT)
    FakeInfo.gate = asyncio.Event()
    browser.fire(NAME, ServiceStateChange.Added)
    await wait_for(lambda: len(FakeInfo.requests) == 1)
    browser.fire(NAME, ServiceStateChange.Removed)
    FakeInfo.gate.set()
    await asyncio.sleep(0.01)
    assert discovery.snapshot() == []
    await discovery.stop()


async def test_stop_cancels_pending_lookups(parts):
    discovery = parts["discovery"]
    await discovery.start()
    (browser,) = parts["browsers"]
    FakeInfo.gate = asyncio.Event()
    browser.fire(NAME, ServiceStateChange.Added)
    await wait_for(lambda: len(FakeInfo.requests) == 1)
    await asyncio.wait_for(discovery.stop(), 1.0)
    FakeInfo.services[NAME] = (["192.0.2.40"], 6053, TXT)
    FakeInfo.gate.set()
    await asyncio.sleep(0.01)
    assert discovery.snapshot() == []


async def test_discovery_that_cannot_start_stays_empty(caplog):
    def broken() -> FakeZeroconf:
        raise OSError("no multicast")

    discovery = Discovery(broken, FakeBrowser, FakeInfo)
    caplog.set_level(logging.WARNING)
    await discovery.start()
    assert discovery.snapshot() == [] and "no multicast" in caplog.text
    await discovery.stop()


async def test_stop_without_start_is_harmless(parts):
    await parts["discovery"].stop()
    assert parts["zc"] == []


async def test_browser_failure_closes_zeroconf(caplog):
    zcs: list[FakeZeroconf] = []

    def zc_factory() -> FakeZeroconf:
        zcs.append(FakeZeroconf())
        return zcs[-1]

    def broken_browser(zeroconf, type_, handlers=None):
        raise OSError("socket closed")

    discovery = Discovery(zc_factory, broken_browser, FakeInfo)
    await discovery.start()
    assert zcs[0].closed and discovery.snapshot() == [] and "socket closed" in caplog.text
    await discovery.stop()


async def test_update_that_cannot_be_resolved_keeps_what_was_found(parts):
    discovery = parts["discovery"]
    await discovery.start()
    (browser,) = parts["browsers"]
    FakeInfo.services[NAME] = (["192.0.2.40"], 6053, TXT)
    browser.fire(NAME, ServiceStateChange.Added)
    await wait_for(lambda: len(discovery.snapshot()) == 1)
    del FakeInfo.services[NAME]
    browser.fire(NAME, ServiceStateChange.Updated)
    await wait_for(lambda: len(FakeInfo.requests) == 2)
    await asyncio.sleep(0.01)
    assert [f.host for f in discovery.snapshot()] == ["192.0.2.40"]
    FakeInfo.services[NAME] = ([], 6053, TXT)
    browser.fire(NAME, ServiceStateChange.Updated)
    await wait_for(lambda: discovery.snapshot() == [])
    await discovery.stop()
