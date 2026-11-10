import asyncio
import ipaddress
import logging
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass
from typing import Any

from zeroconf import ServiceStateChange
from zeroconf.asyncio import AsyncServiceBrowser, AsyncServiceInfo, AsyncZeroconf

log = logging.getLogger(__name__)

SERVICE_TYPE = "_esphomelib._tcp.local."
LOOKUP_TIMEOUT_MS = 3000
PROVISIONING = "zero-psk"


@dataclass(frozen=True)
class Found:
    name: str
    host: str
    port: int
    provisionable: bool
    friendly_name: str


def _text(value: bytes | str | None) -> str:
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return value or ""


def _pick_address(addresses: Sequence[str]) -> str | None:
    parsed = []
    for address in addresses:
        try:
            parsed.append(ipaddress.ip_address(address))
        except ValueError:
            continue
    parsed.sort(key=lambda ip: ip.version)
    return str(parsed[0]) if parsed else None


def parse_service(service_name: str, addresses: Sequence[str], port: int | None,
                  properties: Mapping[bytes | str, bytes | str | None]) -> Found | None:
    suffix = "." + SERVICE_TYPE
    name = service_name[: -len(suffix)] if service_name.endswith(suffix) else ""
    host = _pick_address(addresses)
    if not name or host is None or not port:
        return None
    txt = {_text(key): _text(value) for key, value in properties.items()}
    return Found(
        name=name,
        host=host,
        port=port,
        provisionable=txt.get("api_provisioning") == PROVISIONING,
        friendly_name=txt.get("friendly_name") or name,
    )


class Discovery:
    def __init__(self, zeroconf_factory: Callable[[], Any] = AsyncZeroconf,
                 browser_factory: Callable[..., Any] = AsyncServiceBrowser,
                 info_factory: Callable[[str, str], Any] = AsyncServiceInfo) -> None:
        self._zeroconf_factory = zeroconf_factory
        self._browser_factory = browser_factory
        self._info_factory = info_factory
        self._zeroconf: Any = None
        self._browser: Any = None
        self._found: dict[str, Found] = {}
        self._lookups: dict[str, asyncio.Task] = {}

    async def start(self) -> None:
        try:
            self._zeroconf = self._zeroconf_factory()
            self._browser = self._browser_factory(self._zeroconf.zeroconf, SERVICE_TYPE, handlers=[self._on_change])
        except OSError as e:
            log.warning("looking for speakers on the network is off: %s", e)
            await self.stop()

    async def stop(self) -> None:
        lookups = list(self._lookups.values())
        self._lookups.clear()
        for task in lookups:
            task.cancel()
        await asyncio.gather(*lookups, return_exceptions=True)
        browser, zeroconf, self._browser, self._zeroconf = self._browser, self._zeroconf, None, None
        if browser is not None:
            await browser.async_cancel()
        if zeroconf is not None:
            await zeroconf.async_close()
        self._found.clear()

    def snapshot(self) -> list[Found]:
        return sorted(self._found.values(), key=lambda found: found.name)

    def _on_change(self, zeroconf: Any, service_type: str, name: str, state_change: ServiceStateChange) -> None:
        previous = self._lookups.pop(name, None)
        if previous is not None:
            previous.cancel()
        if state_change is ServiceStateChange.Removed:
            self._found.pop(name, None)
            return
        task = asyncio.get_running_loop().create_task(self._look_up(zeroconf, service_type, name))
        self._lookups[name] = task
        task.add_done_callback(lambda done: self._forget_lookup(name, done))

    def _forget_lookup(self, name: str, task: asyncio.Task) -> None:
        if self._lookups.get(name) is task:
            del self._lookups[name]

    async def _look_up(self, zeroconf: Any, service_type: str, name: str) -> None:
        info = self._info_factory(service_type, name)
        try:
            resolved = await info.async_request(zeroconf, LOOKUP_TIMEOUT_MS)
        except Exception as e:
            log.debug("could not look up %s: %s", name, e)
            return
        if not resolved:
            return
        found = parse_service(name, info.parsed_addresses(), info.port, info.properties)
        if found is None:
            self._found.pop(name, None)
        else:
            self._found[name] = found
