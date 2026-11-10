import asyncio
import time

from aioesphomeapi import VoiceAssistantAudioSettings, VoiceAssistantEventType

from voice_hub.config import Timing
from voice_hub.realtime import Closed, InputCleared, RealtimeError
from voice_hub.streams import AudioStream, AudioStreams

SETTINGS = VoiceAssistantAudioSettings()
TIMING = Timing(
    mic_holdoff_s=0.0,
    first_turn_s=1.0,
    follow_up_s=0.3,
    max_speech_s=2.0,
    response_timeout_s=1.0,
    session_idle_s=0.5,
    mic_stop_grace_s=0.05,
)


class E:
    RUN_START = VoiceAssistantEventType.VOICE_ASSISTANT_RUN_START
    RUN_END = VoiceAssistantEventType.VOICE_ASSISTANT_RUN_END
    STT_START = VoiceAssistantEventType.VOICE_ASSISTANT_STT_START
    STT_VAD_START = VoiceAssistantEventType.VOICE_ASSISTANT_STT_VAD_START
    STT_VAD_END = VoiceAssistantEventType.VOICE_ASSISTANT_STT_VAD_END
    INTENT_PROGRESS = VoiceAssistantEventType.VOICE_ASSISTANT_INTENT_PROGRESS
    INTENT_END = VoiceAssistantEventType.VOICE_ASSISTANT_INTENT_END
    TTS_START = VoiceAssistantEventType.VOICE_ASSISTANT_TTS_START
    TTS_END = VoiceAssistantEventType.VOICE_ASSISTANT_TTS_END
    ERROR = VoiceAssistantEventType.VOICE_ASSISTANT_ERROR


def audio_url(stream_id: str) -> str:
    return f"http://hub:8080/audio/{stream_id}.wav"


async def wait_for(pred, timeout: float = 1.0) -> None:
    deadline = time.monotonic() + timeout
    while not pred():
        if time.monotonic() > deadline:
            raise AssertionError("condition was not met in time")
        await asyncio.sleep(0.005)


class FakeDevice:
    def __init__(self, name: str = "kitchen") -> None:
        self.name = name
        self.volume = 0.7
        self.volume_calls: list[float] = []
        self.events: list[tuple[VoiceAssistantEventType, dict]] = []
        self.times: list[float] = []

    def send_event(self, event: VoiceAssistantEventType, data: dict[str, str] | None = None) -> None:
        self.events.append((event, dict(data or {})))
        self.times.append(time.monotonic())

    def set_volume(self, level: float) -> None:
        self.volume_calls.append(level)
        self.volume = level

    def types(self) -> list[VoiceAssistantEventType]:
        return [event for event, _ in self.events]

    def times_of(self, event: VoiceAssistantEventType) -> list[float]:
        return [t for (e, _), t in zip(self.events, self.times) if e == event]


class FakeSession:
    def __init__(self, fail_connect: bool = False, connect_delay: float = 0.0, fail_on: set[str] | None = None) -> None:
        self.fail_connect = fail_connect
        self.connect_delay = connect_delay
        self.fail_on = set(fail_on or ())
        self.silent = False
        self.clear_delay = 0.0
        self.tools_delay = 0.0
        self.close_delay = 0.0
        self.appended = bytearray()
        self.calls: list = []
        self.trace: list[str] = []
        self.connected = False
        self.closed = False
        self.close_calls = 0
        self._events: asyncio.Queue = asyncio.Queue()

    def push(self, *events) -> None:
        for event in events:
            self._events.put_nowait(event)

    async def connect(self, timeout: float = 10.0) -> None:
        self.trace.append("connect")
        if self.connect_delay:
            await asyncio.sleep(self.connect_delay)
        if self.fail_connect:
            self.closed = True
            raise RealtimeError("connect failed")
        self.connected = True

    async def _send(self, name: str) -> None:
        if not self.connected or self.closed:
            raise RealtimeError("realtime session is not open")
        if name in self.fail_on:
            self.closed = True
            raise RealtimeError(f"{name} failed")
        self.trace.append(name)

    async def append_audio(self, pcm24k: bytes) -> None:
        await self._send("append")
        self.appended += pcm24k

    async def clear_input(self) -> None:
        if self.clear_delay:
            await asyncio.sleep(self.clear_delay)
        await self._send("clear")
        self.calls.append("clear")
        if not self.silent:
            self._events.put_nowait(InputCleared())

    async def commit_input(self) -> None:
        await self._send("commit")
        self.calls.append("commit")

    async def cancel_response(self) -> None:
        await self._send("cancel")
        self.calls.append("cancel")

    async def send_tool_outputs(self, outputs: list[tuple[str, str]], respond: bool = True) -> None:
        if self.tools_delay:
            await asyncio.sleep(self.tools_delay)
        await self._send("tools")
        self.calls.append(("tools", list(outputs), respond))

    async def next_event(self):
        event = await self._events.get()
        if isinstance(event, Closed):
            self.closed = True
        return event

    async def close(self) -> None:
        self.close_calls += 1
        self.closed = True
        if self.close_delay:
            await asyncio.sleep(self.close_delay)
        self._events.put_nowait(Closed())


class SessionFactory:
    def __init__(self, *sessions: FakeSession) -> None:
        self.sessions = list(sessions)
        self.calls = 0

    def __call__(self) -> FakeSession:
        if self.calls == len(self.sessions):
            self.sessions.append(FakeSession())
        session = self.sessions[self.calls]
        self.calls += 1
        return session


class FakeStreams(AudioStreams):
    def __init__(self) -> None:
        super().__init__()
        self.created: list[AudioStream] = []
        self.fail = False

    def create(self) -> AudioStream:
        if self.fail:
            raise RuntimeError("streams are broken")
        stream = super().create()
        self.created.append(stream)
        return stream
