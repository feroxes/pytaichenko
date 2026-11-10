import asyncio
import logging
import uuid
from collections.abc import Awaitable, Callable
from typing import Protocol

from voice_hub.config import Timing
from voice_hub.realtime import Closed, RealtimeError, RealtimeEvent, ResponseDone, ServerError
from voice_hub.tasks import Tasks, cancel_others

log = logging.getLogger(__name__)


class RealtimeLike(Protocol):
    closed: bool

    async def connect(self, timeout: float = 10.0) -> None: ...
    async def append_audio(self, pcm24k: bytes) -> None: ...
    async def clear_input(self) -> None: ...
    async def commit_input(self) -> None: ...
    async def cancel_response(self) -> None: ...
    async def send_tool_outputs(self, outputs: list[tuple[str, str]], respond: bool = True) -> None: ...
    async def next_event(self) -> RealtimeEvent: ...
    async def close(self) -> None: ...


class SessionUser(Protocol):
    session: RealtimeLike | None
    cleared: asyncio.Event


EventHandler = Callable[[RealtimeLike, RealtimeEvent], Awaitable[None]]
TimingSource = Timing | Callable[[], Timing]


class SessionKeeper:
    def __init__(self, name: str, factory: Callable[[], RealtimeLike], tasks: Tasks, on_event: EventHandler,
                 timing: TimingSource = Timing(), config: Callable[[], object] = lambda: None) -> None:
        self._name = name
        self._factory = factory
        self._tasks = tasks
        self._on_event = on_event
        self._timing_source = timing
        self._config_source = config
        self._timing: Timing | None = None
        self._config: object = None
        self._session: RealtimeLike | None = None
        self._reader: asyncio.Task | None = None
        self._idle: asyncio.Task | None = None
        self._forget: Callable[[], None] = lambda: None
        self.cid = uuid.uuid4().hex

    @property
    def timing(self) -> Timing:
        if self._session is not None and self._timing is not None:
            return self._timing
        return self._current_timing()

    @property
    def fresh_timing(self) -> Timing:
        return self._current_timing()

    async def for_turn(self, turn: SessionUser, cancel_previous: bool) -> RealtimeLike:
        if self._session is not None and self._outdated():
            log.info("settings changed; opening a fresh realtime session for %s", self._name)
            self._tasks.spawn(self.close(self.detach()))
        if self._session is not None and cancel_previous:
            try:
                await self._session.cancel_response()
            except RealtimeError as e:
                log.warning("realtime cancel failed on %s: %s", self._name, e)
                await self.close(self.detach())
        if self._session is not None and self._session.closed:
            await self.close(self.detach())
        if self._session is not None:
            session = turn.session = self._session
            if await self._answers(turn, session, self.timing.ready_check_s):
                return session
            log.warning("realtime session for %s did not answer; reconnecting", self._name)
            if self._session is session:
                self.detach()
                self._tasks.spawn(self.close(session))
        session = turn.session = await self._open()
        await session.clear_input()
        return session

    async def release(self, session: RealtimeLike | None, *, cancel: bool, clear: bool,
                      drop: bool) -> RealtimeLike | None:
        if session is None or session is not self._session:
            return None
        try:
            if cancel:
                await session.cancel_response()
            if clear:
                await session.clear_input()
        except RealtimeError as e:
            log.warning("realtime send failed on %s: %s", self._name, e)
            drop = True
        return self.detach() if drop and session is self._session else None

    def cancel_idle(self) -> None:
        if self._idle is not None:
            self._idle.cancel()
            self._idle = None

    def close_when_idle(self, delay: float, on_close: Callable[[], None]) -> None:
        self.cancel_idle()
        if self._session is None:
            on_close()
            return
        self._forget = on_close
        self._idle = self._tasks.spawn(self._idle_close(delay))

    def detach(self) -> RealtimeLike | None:
        session, reader, idle = self._session, self._reader, self._idle
        self._session = self._reader = self._idle = None
        cancel_others((reader, idle))
        if idle is not None and idle is not asyncio.current_task():
            self._forget()
        return session

    async def close(self, session: RealtimeLike | None) -> None:
        if session is None:
            return
        try:
            await session.close()
        except Exception:
            log.exception("could not close the realtime session for %s", self._name)

    def _current_timing(self) -> Timing:
        source = self._timing_source
        return source() if callable(source) else source

    def _outdated(self) -> bool:
        return self._timing != self._current_timing() or self._config != self._config_source()

    async def _open(self) -> RealtimeLike:
        self.cid = uuid.uuid4().hex
        timing, config, session = self._current_timing(), self._config_source(), self._factory()
        await session.connect()
        self._session, self._timing, self._config = session, timing, config
        self._reader = self._tasks.spawn(self._read(session))
        log.info("realtime session opened for %s", self._name)
        return session

    async def _answers(self, turn: SessionUser, session: RealtimeLike, ready_check_s: float) -> bool:
        try:
            async with asyncio.timeout(ready_check_s):
                await session.clear_input()
                await turn.cleared.wait()
        except (RealtimeError, TimeoutError):
            return False
        return self._session is session

    async def _read(self, session: RealtimeLike) -> None:
        while self._session is session:
            try:
                event = await session.next_event()
            except Exception:
                log.exception("realtime read failed on %s", self._name)
                event = Closed()
            if self._session is not session:
                return
            if isinstance(event, ServerError):
                log.warning("realtime error on %s: %s %s", self._name, event.code, event.message)
                continue
            if isinstance(event, ResponseDone):
                log.info("response %s on %s: %s, usage %s", event.response_id, self._name, event.status, event.usage)
            try:
                await self._on_event(session, event)
            except Exception:
                log.exception("failed to handle %s on %s", type(event).__name__, self._name)
            if isinstance(event, Closed):
                return

    async def _idle_close(self, delay: float) -> None:
        await asyncio.sleep(delay)
        log.info("closing idle realtime session for %s", self._name)
        self._forget()
        await self.close(self.detach())
