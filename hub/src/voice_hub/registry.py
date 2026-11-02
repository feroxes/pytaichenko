import asyncio
import logging
from collections.abc import Callable, Iterator, Mapping
from dataclasses import dataclass, replace
from typing import Any, Protocol

from voice_hub.adopt import Adopted, AdoptError, ClientFactory, adopt, identify
from voice_hub.config import OpenAIConfig, Timing
from voice_hub.conversation import Conversation
from voice_hub.device import DeviceLink, VolumeError, WakeWordError, WakeWords, make_client
from voice_hub.session import RealtimeLike
from voice_hub.settings import (
    DEFAULT_DEVICE_PORT,
    DeviceSettings,
    Settings,
    SettingsError,
    SettingsStore,
    apply_patch,
    check_address,
    check_key,
    device_config,
    openai_config,
)
from voice_hub.streams import AudioStreams

log = logging.getLogger(__name__)


class LinkLike(Protocol):
    name: str
    connected: bool
    wake_words: WakeWords

    @property
    def player_volume(self) -> float | None: ...

    async def start(self) -> None: ...
    async def stop(self) -> None: ...
    async def set_wake_word(self, wake_word_id: str) -> None: ...
    def change_volume(self, level: float) -> None: ...


def _percent(level: float | None) -> int | None:
    return None if level is None else min(100, max(0, round(level * 100)))


@dataclass(frozen=True)
class _Speaker:
    device: DeviceSettings
    link: LinkLike | None = None

    def keeps(self, wanted: DeviceSettings | None) -> bool:
        return wanted == self.device and (self.link is not None or wanted.key is None)

    def status(self) -> dict[str, Any]:
        d, link = self.device, self.link
        live = link if link is not None and link.connected else None
        return {
            "name": d.name,
            "host": d.host,
            "port": d.port,
            "key_set": d.key is not None,
            "connected": live is not None,
            "wake_words": (link.wake_words if link is not None else WakeWords()).view(),
            "volume": _percent(live.player_volume) if live is not None else None,
        }


class DeviceRegistry:
    def __init__(self, make_link: Callable[[DeviceSettings], LinkLike]) -> None:
        self._make_link = make_link
        self._speakers: dict[str, _Speaker] = {}
        self._lock = asyncio.Lock()
        self._closed = False

    def __iter__(self) -> Iterator[LinkLike]:
        return iter([s.link for s in self._speakers.values() if s.link is not None])

    def link(self, name: str) -> LinkLike | None:
        speaker = self._speakers.get(name)
        return speaker.link if speaker is not None else None

    def status(self) -> list[dict[str, Any]]:
        return [speaker.status() for speaker in self._speakers.values()]

    def health(self) -> dict[str, bool]:
        return {name: s.link is not None and s.link.connected for name, s in self._speakers.items()
                if s.device.key is not None}

    async def reconcile(self, settings: Settings) -> None:
        async with self._lock:
            if self._closed:
                return
            wanted = {d.name: d for d in settings.devices}
            stale = [name for name, speaker in self._speakers.items() if not speaker.keeps(wanted.get(name))]
            await self._stop([self._speakers.pop(name) for name in stale])
            speakers: dict[str, _Speaker] = {}
            for device in settings.devices:
                speakers[device.name] = self._speakers.get(device.name) or await self._start(device)
            self._speakers = speakers

    async def close(self) -> None:
        async with self._lock:
            self._closed = True
            speakers, self._speakers = list(self._speakers.values()), {}
            await self._stop(speakers)

    async def _start(self, device: DeviceSettings) -> _Speaker:
        if device.key is None:
            log.warning("speaker %s has no key yet and is left offline", device.name)
            return _Speaker(device)
        try:
            link = self._make_link(device)
        except Exception:
            log.exception("could not set up speaker %s", device.name)
            return _Speaker(device)
        try:
            await link.start()
        except Exception:
            log.exception("could not start speaker %s", device.name)
            await self._stop([_Speaker(device, link)])
            return _Speaker(device)
        log.info("speaker %s at %s:%d added", device.name, device.host, device.port)
        return _Speaker(device, link)

    async def _stop(self, speakers: list[_Speaker]) -> None:
        links = [speaker.link for speaker in speakers if speaker.link is not None]
        results = await asyncio.gather(*(link.stop() for link in links), return_exceptions=True)
        for link, result in zip(links, results):
            if isinstance(result, BaseException):
                log.error("could not stop %s cleanly", link.name, exc_info=result)
            else:
                log.info("speaker %s removed", link.name)


class Hub:
    def __init__(self, store: SettingsStore, settings: Settings, registry: DeviceRegistry,
                 client_factory: ClientFactory = make_client) -> None:
        self._store = store
        self._settings = settings
        self.registry = registry
        self._client_factory = client_factory
        self._lock = asyncio.Lock()

    @property
    def settings(self) -> Settings:
        return self._settings

    async def start(self) -> None:
        await self.registry.reconcile(self._settings)

    async def close(self) -> None:
        await self.registry.close()

    def status(self) -> list[dict[str, Any]]:
        return self.registry.status()

    async def update_settings(self, patch: Mapping[str, Any]) -> Settings:
        return await self.change(lambda current: apply_patch(current, patch))

    async def change(self, edit: Callable[[Settings], Settings]) -> Settings:
        async with self._lock:
            return await self._apply(edit(self._settings))

    async def adopt(self, host: str, port: int = DEFAULT_DEVICE_PORT) -> str:
        host, port = check_address(host, port)
        async with self._lock:
            before = self._settings
            kept: list[Settings] = []

            def keep(adopted: Adopted) -> None:
                settings = self._with_adopted(before, adopted, host, port)
                self._store.save(settings)
                self._settings = settings
                kept.append(settings)

            try:
                adopted = await adopt(host, port, self._client_factory, keep)
            except AdoptError:
                if kept:
                    self._take_back(before)
                raise
            await self.registry.reconcile(self._settings)
        return adopted.name

    async def add(self, host: str, port: int, key: str) -> str:
        host, port = check_address(host, port)
        key = check_key(key)
        name = await identify(host, port, key, self._client_factory)
        async with self._lock:
            await self._apply(self._with_adopted(self._settings, Adopted(name, key), host, port))
        log.info("speaker %s at %s:%d added with its own key", name, host, port)
        return name

    async def set_wake_word(self, name: str, wake_word_id: str) -> None:
        link = self.registry.link(name)
        if link is None:
            raise WakeWordError(f"no speaker called {name!r} is set up", "unknown_speaker")
        await link.set_wake_word(wake_word_id)

    def set_volume(self, name: str, percent: int) -> None:
        link = self.registry.link(name)
        if link is None:
            raise VolumeError(f"no speaker called {name!r} is set up", "unknown_speaker")
        link.change_volume(percent / 100)

    def _with_adopted(self, settings: Settings, adopted: Adopted, host: str, port: int) -> Settings:
        same = next((d for d in settings.devices if d.name == adopted.name), None)
        link = self.registry.link(adopted.name)
        if same is not None and (same.host != host or (link is not None and link.connected)):
            raise AdoptError(f"another speaker is already called {adopted.name!r}", "name_taken")
        return _with_device(settings, adopted.name, host, port, adopted.key)

    def _take_back(self, before: Settings) -> None:
        try:
            self._store.save(before)
        except SettingsError as e:
            log.error("could not take back the refused speaker from the settings: %s", e)
        self._settings = before

    async def _apply(self, settings: Settings) -> Settings:
        self._store.save(settings)
        self._settings = settings
        await self.registry.reconcile(settings)
        return settings


def _with_device(settings: Settings, name: str, host: str, port: int, key: str) -> Settings:
    devices = list(settings.devices)
    for i, device in enumerate(devices):
        if device.name == name:
            devices[i] = replace(device, host=host, port=port, key=key)
            break
    else:
        devices.append(DeviceSettings(name=name, host=host, port=port, key=key))
    return replace(settings, devices=tuple(devices))


def link_factory(current: Callable[[], Settings], streams: AudioStreams, audio_url: Callable[[str], str],
                 new_session: Callable[[OpenAIConfig], RealtimeLike],
                 debug_input: Callable[[str], bytes]) -> Callable[[DeviceSettings], DeviceLink]:
    def config() -> OpenAIConfig:
        return openai_config(current())

    def session_factory() -> RealtimeLike:
        return new_session(config())

    def timing() -> Timing:
        return current().timing

    def make_link(device: DeviceSettings) -> DeviceLink:
        wav = debug_input(device.debug_input_wav) if device.debug_input_wav else None

        def make_conversation(link: DeviceLink) -> Conversation:
            return Conversation(device=link, streams=streams, session_factory=session_factory, timing=timing,
                                audio_url=audio_url, debug_input=wav, config=config)

        return DeviceLink(device_config(device), make_conversation)

    return make_link
