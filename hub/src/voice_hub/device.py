import asyncio
import contextlib
import logging
from collections.abc import Callable
from dataclasses import dataclass
from typing import Any

from aioesphomeapi import (
    APIClient,
    APIConnectionError,
    EntityState,
    MediaPlayerEntityState,
    MediaPlayerInfo,
    ReconnectLogic,
    VoiceAssistantEventType,
)

from voice_hub.config import DeviceConfig
from voice_hub.conversation import Conversation

log = logging.getLogger(__name__)

INITIAL_VOLUME = 0.7
CLOSE_WAIT_S = 1.0
WAKE_WORDS_TIMEOUT_S = 5.0
CLIENT_INFO = "voice-hub"


def make_client(host: str, port: int, key: str, name: str | None = None) -> APIClient:
    return APIClient(host, port, None, noise_psk=key, client_info=CLIENT_INFO, expected_name=name)


class WakeWordError(Exception):
    def __init__(self, message: str, code: str) -> None:
        super().__init__(message)
        self.code = code


class VolumeError(Exception):
    def __init__(self, message: str, code: str) -> None:
        super().__init__(message)
        self.code = code


@dataclass(frozen=True)
class WakeWords:
    available: tuple[tuple[str, str], ...] = ()
    active: tuple[str, ...] = ()

    def view(self) -> dict[str, Any]:
        return {"available": list(self.available), "active": list(self.active)}


class DeviceLink:
    def __init__(self, cfg: DeviceConfig, make_conversation: Callable[["DeviceLink"], Conversation],
                 client: APIClient | None = None) -> None:
        self.name = cfg.name
        self.connected = False
        self.volume = INITIAL_VOLUME
        self.wake_words = WakeWords()
        self._client = client or make_client(cfg.host, cfg.port, cfg.encryption_key, cfg.name)
        self.conversation = make_conversation(self)
        self._reconnect: ReconnectLogic | None = None
        self._player: tuple[int, int] | None = None
        self._unsubscribe: Callable[[], None] | None = None
        self._closing: set[asyncio.Task] = set()
        self._wake_read: asyncio.Task | None = None

    def send_event(self, event: VoiceAssistantEventType, data: dict[str, str] | None = None) -> None:
        if not self.connected:
            log.debug("dropping %s for %s: not connected", event.name, self.name)
            return
        try:
            self._client.send_voice_assistant_event(event, data)
        except APIConnectionError as e:
            log.warning("could not send %s to %s: %s", event.name, self.name, e)

    @property
    def player_volume(self) -> float | None:
        return self.volume if self.connected and self._player is not None else None

    def set_volume(self, level: float) -> None:
        with contextlib.suppress(VolumeError):
            self.change_volume(level)

    def change_volume(self, level: float) -> None:
        if not self.connected or self._player is None:
            log.debug("not setting volume on %s: not connected or no media player", self.name)
            raise VolumeError(f"{self.name} is not connected or has no media player", "offline")
        key, device_id = self._player
        try:
            self._client.media_player_command(key, volume=level, device_id=device_id)
        except APIConnectionError as e:
            log.warning("could not set volume on %s: %s", self.name, e)
            raise VolumeError(f"could not set the volume on {self.name}", "failed") from None
        self.volume = level

    async def on_connect(self) -> None:
        try:
            await self._attach()
        except Exception:
            log.exception("could not set up %s after connecting, dropping the connection", self.name)
            self._forget()
            await self._client.disconnect()
            return
        self.connected = True
        log.info("device %s connected", self.name)
        self._wake_read = asyncio.create_task(self._load_wake_words())

    async def set_wake_word(self, wake_word_id: str) -> None:
        if not self.connected:
            raise WakeWordError(f"{self.name} is not connected", "offline")
        if wake_word_id not in {known for known, _ in self.wake_words.available}:
            raise WakeWordError(f"{self.name} has no wake word {wake_word_id!r}", "unknown")
        try:
            await self._client.set_voice_assistant_configuration([wake_word_id])
            self.wake_words = await self._read_wake_words()
        except APIConnectionError as e:
            log.warning("could not change the wake word on %s: %s", self.name, e)
            raise WakeWordError(f"could not change the wake word on {self.name}", "failed") from None
        log.info("wake word on %s set to %s", self.name, wake_word_id)

    async def on_disconnect(self, expected_disconnect: bool) -> None:
        self._forget()
        kind = "expected" if expected_disconnect else "unexpected"
        log.info("device %s disconnected (%s)", self.name, kind)
        await self._close_conversation()

    async def start(self) -> None:
        if self._reconnect is not None:
            return
        self._reconnect = ReconnectLogic(
            client=self._client, on_connect=self.on_connect, on_disconnect=self.on_disconnect, name=self.name
        )
        await self._reconnect.start()

    async def stop(self) -> None:
        self._cancel_wake_read()
        try:
            if self._reconnect is not None:
                await self._reconnect.stop()
            await self._client.disconnect()
        finally:
            await self.conversation.close()
            await asyncio.gather(*self._closing, return_exceptions=True)

    async def _attach(self) -> None:
        entities, _ = await self._client.list_entities_services()
        player = next((e for e in entities if isinstance(e, MediaPlayerInfo)), None)
        if player is None:
            log.warning("no media player found on %s, volume control is off", self.name)
            self._player = None
        else:
            self._player = (player.key, player.device_id)
        self._client.subscribe_states(self._on_state)
        self._unsubscribe = self._client.subscribe_voice_assistant(
            handle_start=self.conversation.handle_start,
            handle_stop=self.conversation.handle_stop,
            handle_audio=self.conversation.handle_audio,
        )

    async def _load_wake_words(self) -> None:
        try:
            wake_words = await self._read_wake_words()
        except Exception as e:
            log.warning("could not read the wake words of %s: %s", self.name, e,
                        exc_info=not isinstance(e, APIConnectionError))
            return
        if self.connected:
            self.wake_words = wake_words

    def _cancel_wake_read(self) -> None:
        task, self._wake_read = self._wake_read, None
        if task is not None:
            task.cancel()
            self._closing.add(task)
            task.add_done_callback(self._closing.discard)

    async def _read_wake_words(self) -> WakeWords:
        config = await self._client.get_voice_assistant_configuration(WAKE_WORDS_TIMEOUT_S)
        available = tuple((word.id, word.wake_word) for word in config.available_wake_words)
        return WakeWords(available, tuple(config.active_wake_words))

    def _forget(self) -> None:
        self._cancel_wake_read()
        self.connected = False
        self.wake_words = WakeWords()
        self._player = None
        self._unsubscribe = None

    def _on_state(self, state: EntityState) -> None:
        if self._player is not None and isinstance(state, MediaPlayerEntityState) and state.key == self._player[0]:
            self.volume = state.volume

    async def _close_conversation(self) -> None:
        task = asyncio.create_task(self.conversation.close())
        self._closing.add(task)
        task.add_done_callback(self._closing.discard)
        task.add_done_callback(self._report_close)
        await asyncio.wait({task}, timeout=CLOSE_WAIT_S)

    def _report_close(self, task: asyncio.Task) -> None:
        if not task.cancelled() and task.exception() is not None:
            log.error("could not close the conversation for %s", self.name, exc_info=task.exception())
