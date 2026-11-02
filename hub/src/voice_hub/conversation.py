import asyncio
import logging
import time
from collections.abc import Awaitable, Callable
from dataclasses import dataclass, field
from enum import Enum, auto

from aioesphomeapi import VoiceAssistantAudioSettings

from voice_hub import voice_events
from voice_hub.pipeline import DEVICE_BYTES_PER_S, AudioPath, feed_debug
from voice_hub.realtime import (
    AudioDelta, Closed, InputCleared, InputTranscript, RealtimeError, RealtimeEvent, ResponseCreated, ResponseDone,
    SpeechStarted, SpeechStopped, TranscriptDone,
)
from voice_hub.session import RealtimeLike, SessionKeeper, TimingSource
from voice_hub.streams import AudioStream, AudioStreams
from voice_hub.tasks import Group, Tasks
from voice_hub.tool_runner import ends_conversation, run_tools
from voice_hub.voice_events import DevicePort, Events

log = logging.getLogger(__name__)

FINAL_STATUSES = ("completed", "incomplete")
UNAVAILABLE = "openai-unavailable"


class Phase(Enum):
    LISTENING = auto()
    SPEAKING = auto()
    ANSWERING = auto()


@dataclass(eq=False)
class Turn:
    stream: AudioStream
    url: str
    follow_up: bool
    follow_ups: int
    tasks: Group
    audio_path: AudioPath
    phase: Phase = Phase.LISTENING
    announced: bool = False
    ready: bool = False
    closed: bool = False
    stopped: bool = False
    session: RealtimeLike | None = None
    cleared: asyncio.Event = field(default_factory=asyncio.Event)
    audio: asyncio.Queue[bytes | None] = field(default_factory=asyncio.Queue)
    response_id: str | None = None
    streaming: bool = False
    transcript: list[str] = field(default_factory=list)
    vad_end_at: float | None = None

    @property
    def hearing(self) -> bool:
        return not self.closed and self.phase is not Phase.ANSWERING

    def hear(self, pcm: bytes) -> None:
        if self.hearing and (pcm := self.audio_path.from_mic(pcm)):
            self.audio.put_nowait(pcm)

    async def grace(self, seconds: float) -> None:
        if self.vad_end_at is None:
            return
        while (delay := self.vad_end_at + seconds - time.monotonic()) > 0:
            await asyncio.sleep(delay)


class Conversation:
    def __init__(self, device: DevicePort, streams: AudioStreams, session_factory: Callable[[], RealtimeLike],
                 timing: TimingSource, audio_url: Callable[[str], str], debug_input: bytes | None = None,
                 config: Callable[[], object] = lambda: None) -> None:
        self._device = device
        self._streams = streams
        self._audio_url = audio_url
        self._debug_input = debug_input
        self._tasks = Tasks(device.name)
        self._sessions = SessionKeeper(device.name, session_factory, self._tasks, self._on_event, timing, config)
        self._turn: Turn | None = None
        self._follow_up = False
        self._follow_ups = 0

    async def handle_start(self, conversation_id: str, flags: int, audio_settings: VoiceAssistantAudioSettings,
                           wake_word_phrase: str | None) -> int | None:
        stream = None
        try:
            stream = self._streams.create()
            return self._begin(stream)
        except Exception:
            if stream is not None:
                self._streams.discard(stream)
            log.exception("could not start a turn on %s", self._device.name)
            return None

    async def handle_audio(self, data: bytes, data2: bytes | None = None) -> None:
        turn = self._turn
        if turn is not None and self._debug_input is None:
            turn.hear(data)

    async def handle_stop(self, abort: bool) -> None:
        self._follow_up = False
        turn = self._turn
        if turn is None:
            log.info("stop from %s with no active turn; follow-up cleared", self._device.name)
            voice_events.send(self._device, voice_events.run_ended())
            return
        if turn.closed:
            log.info("stop from %s while the turn is ending; follow-up cleared", self._device.name)
            turn.stopped = True
            return
        cancel = turn.phase is not Phase.LISTENING
        await self._end(turn, "stopped by device", voice_events.run_ended(), cancel=cancel, clear=True)

    async def close(self) -> None:
        turn, self._turn = self._turn, None
        self._follow_up = False
        if turn is not None:
            self._abandon(turn, "conversation closed")
        session = self._sessions.detach()
        await self._tasks.close()
        await self._sessions.close(session)

    def _begin(self, stream: AudioStream) -> int:
        self._follow_ups = self._follow_ups + 1 if self._follow_up else 0
        turn = Turn(stream, self._audio_url(stream.id), self._follow_up, self._follow_ups, Group(self._tasks),
                    AudioPath(self._sessions.fresh_timing.mic_holdoff_s, raw=self._debug_input is not None))
        self._follow_up = False
        old = self._turn
        cancel_previous = old is not None and not old.closed and old.phase is not Phase.LISTENING
        if old is not None:
            self._abandon(old, "replaced by a new start")
        self._sessions.cancel_idle()
        self._turn = turn
        turn.tasks.spawn(self._prepare(turn, cancel_previous))
        if self._debug_input is not None:
            data = b"" if turn.follow_up else self._debug_input
            turn.tasks.spawn(feed_debug(data, turn.hear, lambda: turn.hearing))
        log.info("turn started on %s (%s)", self._device.name, "follow-up" if turn.follow_up else "first turn")
        return 0

    def _abandon(self, turn: Turn, reason: str) -> None:
        if not turn.closed:
            turn.closed = True
            log.info("turn on %s ended: %s", self._device.name, reason)
        turn.tasks.cancel()
        turn.stream.finish()

    async def _prepare(self, turn: Turn, cancel_previous: bool) -> None:
        await asyncio.sleep(0)
        self._announce(turn)
        try:
            session = await self._sessions.for_turn(turn, cancel_previous)
        except Exception as e:
            trace = not isinstance(e, RealtimeError)
            log.warning("realtime unavailable on %s: %s", self._device.name, e, exc_info=trace)
            await self._fail(turn, UNAVAILABLE, "OpenAI is unavailable")
            return
        turn.ready = True
        listen_s = self._sessions.timing.follow_up_s if turn.follow_up else self._sessions.timing.first_turn_s
        turn.tasks.arm(listen_s, lambda: self._no_speech(turn))
        while (chunk := await turn.audio.get()) is not None:
            if not turn.hearing:
                return
            if not await self._send_or_fail(turn, session.append_audio(chunk)):
                return

    async def _on_event(self, session: RealtimeLike, event: RealtimeEvent) -> None:
        turn = self._turn
        if isinstance(event, Closed):
            log.info("realtime session closed for %s", self._device.name)
            if turn is not None and not turn.closed and turn.ready and turn.session is session:
                await self._fail(turn, UNAVAILABLE, "OpenAI connection lost")
                return
            dead = self._sessions.detach()
            if turn is not None and turn.session is session:
                turn.cleared.set()
            await self._sessions.close(dead)
            return
        if isinstance(event, InputCleared):
            if turn is not None and turn.session is session:
                turn.cleared.set()
            return
        if isinstance(event, InputTranscript):
            log.info("heard on %s: %s", self._device.name, event.text)
            return
        if turn is None or turn.closed or not turn.ready or turn.session is not session:
            return
        match event:
            case SpeechStarted() if turn.phase is Phase.LISTENING:
                log.info("speech started on %s", self._device.name)
                turn.phase = Phase.SPEAKING
                self._send(turn, voice_events.speech_started())
                turn.tasks.arm(self._sessions.timing.max_speech_s, lambda: self._speech_limit(turn))
            case SpeechStopped() if turn.phase is Phase.SPEAKING:
                log.info("speech stopped on %s", self._device.name)
                await self._answer(turn, commit=False)
            case ResponseCreated(response_id=rid) if turn.phase is Phase.ANSWERING and turn.response_id is None:
                turn.response_id = rid
                self._expect_answer(turn)
            case AudioDelta(response_id=rid) if rid == turn.response_id:
                self._expect_answer(turn)
                await self._on_audio(turn, event.pcm)
            case TranscriptDone(response_id=rid) if rid == turn.response_id:
                self._expect_answer(turn)
                turn.transcript.append(event.text)
            case ResponseDone(response_id=rid) if rid == turn.response_id:
                await self._on_response_done(turn, session, event)

    async def _answer(self, turn: Turn, commit: bool) -> None:
        turn.phase = Phase.ANSWERING
        turn.audio.put_nowait(None)
        self._send(turn, voice_events.speech_stopped())
        turn.vad_end_at = time.monotonic()
        self._expect_answer(turn)
        turn.tasks.later(self._sessions.timing.answer_max_s, lambda: self._answer_limit(turn))
        if commit and turn.session is not None:
            await self._send_or_fail(turn, turn.session.commit_input())

    def _expect_answer(self, turn: Turn) -> None:
        timeout = self._sessions.timing.response_timeout_s
        turn.tasks.arm(timeout, lambda: self._fail(turn, "timeout", "OpenAI did not answer in time"))

    async def _answer_limit(self, turn: Turn) -> None:
        log.info("answer limit reached on %s", self._device.name)
        await self._fail(turn, "timeout", "OpenAI answer took too long", cancel=True)

    async def _speech_limit(self, turn: Turn) -> None:
        if turn.phase is Phase.SPEAKING:
            log.info("speech limit reached on %s", self._device.name)
            await self._answer(turn, commit=True)

    async def _no_speech(self, turn: Turn) -> None:
        if turn.phase is Phase.LISTENING:
            await self._end(turn, "no speech", voice_events.run_ended(), clear=True)

    async def _on_audio(self, turn: Turn, pcm: bytes) -> None:
        if not turn.streaming:
            turn.streaming = True
            latency = time.monotonic() - (turn.vad_end_at or time.monotonic())
            log.info("first audio on %s %.2f s after speech end", self._device.name, latency)
            await turn.grace(self._sessions.timing.mic_stop_grace_s)
            if turn.closed:
                return
            self._send(turn, voice_events.answer_streaming())
        turn.stream.write(turn.audio_path.to_device(pcm))

    async def _on_response_done(self, turn: Turn, session: RealtimeLike, event: ResponseDone) -> None:
        turn.response_id = None
        if event.status not in FINAL_STATUSES:
            await self._fail(turn, "openai-error", event.error or f"response {event.status}")
        elif event.tool_calls:
            ending = ends_conversation(event.tool_calls)
            self._expect_answer(turn)
            outputs = run_tools(self._device, event.tool_calls)
            sent = await self._send_or_fail(turn, session.send_tool_outputs(outputs, respond=not ending))
            if not sent or turn.closed:
                return
            if ending:
                await self._complete(turn, ended=True)
            else:
                self._expect_answer(turn)
        else:
            await self._complete(turn, ended=False)

    async def _complete(self, turn: Turn, ended: bool) -> None:
        turn.stream.write(turn.audio_path.flush())
        events = voice_events.answered(self._sessions.cid, turn.transcript, turn.url, turn.streaming, ended)
        more = not ended and turn.follow_ups < self._sessions.timing.max_follow_ups
        reason = "ended by model" if ended else "answered" if more else "answered, follow-up limit reached"
        await self._end(turn, reason, events, follow_up=more)

    async def _send_or_fail(self, turn: Turn, send: Awaitable[None]) -> bool:
        try:
            await send
        except RealtimeError as e:
            log.warning("realtime send failed on %s: %s", self._device.name, e)
            await self._fail(turn, UNAVAILABLE, "OpenAI connection lost")
            return False
        return True

    async def _fail(self, turn: Turn, code: str, message: str, cancel: bool = False) -> None:
        events = voice_events.failed(self._sessions.cid, code, message)
        await self._end(turn, f"error {code}", events, cancel=cancel, drop=True)

    async def _end(self, turn: Turn, reason: str, events: Events, *, follow_up: bool = False, cancel: bool = False,
                   clear: bool = False, drop: bool = False) -> None:
        if turn.closed:
            return
        turn.closed = True
        turn.tasks.cancel()
        turn.stream.finish()
        log.info("turn on %s ended: %s", self._device.name, reason)
        log.info("device audio on %s: %s", self._device.name, turn.audio_path.mic_summary())
        dropped = await self._sessions.release(turn.session, cancel=cancel, clear=clear, drop=drop)
        try:
            await turn.grace(self._sessions.timing.mic_stop_grace_s)
            if self._turn is turn:
                self._announce(turn)
                self._turn = None
                follow_up = follow_up and not turn.stopped
                voice_events.send(self._device, events, follow_up)
                self._follow_up = follow_up
                idle_s = self._sessions.timing.session_idle_s + turn.stream.written / DEVICE_BYTES_PER_S
                self._sessions.close_when_idle(idle_s, self._forget_follow_up)
        finally:
            await self._sessions.close(dropped)

    def _forget_follow_up(self) -> None:
        self._follow_up = False

    def _announce(self, turn: Turn) -> None:
        if not turn.announced:
            turn.announced = True
            self._send(turn, voice_events.turn_started(turn.url))

    def _send(self, turn: Turn, events: Events) -> None:
        if self._turn is turn:
            voice_events.send(self._device, events)
