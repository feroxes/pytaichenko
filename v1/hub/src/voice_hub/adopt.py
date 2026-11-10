import asyncio
import base64
import contextlib
import logging
import secrets
from collections.abc import Callable
from dataclasses import dataclass, field
from typing import Protocol

from aioesphomeapi import (
    ZERO_NOISE_PSK,
    APIConnectionError,
    DeviceInfo,
    EncryptionPlaintextAPIError,
    InvalidEncryptionKeyAPIError,
)

log = logging.getLogger(__name__)

KEY_BYTES = 32
CONNECT_TIMEOUT_S = 15.0
SET_KEY_TIMEOUT_S = 15.0
PROBE_DELAY_S = 1.0
UNREACHABLE = (APIConnectionError, OSError, TimeoutError)


class AdoptError(Exception):
    def __init__(self, message: str, code: str) -> None:
        super().__init__(message)
        self.code = code


@dataclass(frozen=True)
class Adopted:
    name: str
    key: str = field(repr=False)


class AdoptClient(Protocol):
    async def connect(self, on_stop=None, login: bool = False) -> None: ...
    async def device_info(self) -> DeviceInfo: ...
    async def noise_encryption_set_key(self, key: bytes) -> bool: ...
    async def disconnect(self, force: bool = False) -> None: ...


ClientFactory = Callable[[str, int, str], AdoptClient]


async def adopt(host: str, port: int, client_factory: ClientFactory, keep: Callable[[Adopted], None]) -> Adopted:
    client = client_factory(host, port, ZERO_NOISE_PSK)
    try:
        adopted = Adopted(await _ask_name(client, host), base64.b64encode(secrets.token_bytes(KEY_BYTES)).decode())
        keep(adopted)
        taken = await _send_key(client, adopted.key)
    finally:
        await _disconnect(client)
    if taken is None:
        log.warning("%s did not confirm the new key, checking again", host)
        taken = await _has_key_now(host, port, client_factory)
    elif taken:
        await asyncio.sleep(PROBE_DELAY_S)
    if taken is False:
        raise AdoptError(f"{host} refused the new key", "refused")
    if taken is None:
        log.warning("could not check whether %s took the new key; keeping it", host)
    log.info("speaker %s at %s now has its own key", adopted.name, host)
    return adopted


async def _device_info(client: AdoptClient) -> DeviceInfo:
    async with asyncio.timeout(CONNECT_TIMEOUT_S):
        await client.connect(login=False)
        return await client.device_info()


async def identify(host: str, port: int, key: str, client_factory: ClientFactory) -> str:
    client = client_factory(host, port, key)
    try:
        info = await _reach(client, host, AdoptError(f"{host} has a different key", "wrong_key"))
    except AdoptError as e:
        failure = e
    else:
        return _name(info, host)
    finally:
        await _disconnect(client)
    if failure.code == "wrong_key" and await _key_state(host, port, client_factory) is False:
        raise AdoptError(f"{host} has no key yet; add it without a key to give it one", "no_key_yet") from None
    raise failure


async def _reach(client: AdoptClient, host: str, wrong_key: AdoptError) -> DeviceInfo:
    try:
        return await _device_info(client)
    except InvalidEncryptionKeyAPIError:
        raise wrong_key from None
    except EncryptionPlaintextAPIError:
        raise AdoptError(f"{host} does not support encryption keys", "unsupported") from None
    except UNREACHABLE:
        raise AdoptError(f"could not connect to {host}", "unreachable") from None


def _name(info: DeviceInfo, host: str) -> str:
    name = info.name.strip()
    if not name:
        raise AdoptError(f"{host} did not tell its name", "no_name")
    return name


async def _ask_name(client: AdoptClient, host: str) -> str:
    info = await _reach(client, host, AdoptError(f"{host} already has a key", "has_key"))
    if not info.api_encryption_provisionable:
        raise AdoptError(f"{host} already has a key", "has_key")
    return _name(info, host)


async def _send_key(client: AdoptClient, key: str) -> bool | None:
    try:
        async with asyncio.timeout(SET_KEY_TIMEOUT_S):
            return await client.noise_encryption_set_key(key.encode("ascii"))
    except UNREACHABLE:
        return None


async def _has_key_now(host: str, port: int, client_factory: ClientFactory) -> bool | None:
    await asyncio.sleep(PROBE_DELAY_S)
    return await _key_state(host, port, client_factory)


async def _key_state(host: str, port: int, client_factory: ClientFactory) -> bool | None:
    client = client_factory(host, port, ZERO_NOISE_PSK)
    try:
        info = await _device_info(client)
    except InvalidEncryptionKeyAPIError:
        return True
    except UNREACHABLE:
        return None
    finally:
        await _disconnect(client)
    return not info.api_encryption_provisionable


async def _disconnect(client: AdoptClient) -> None:
    with contextlib.suppress(*UNREACHABLE):
        await client.disconnect()
