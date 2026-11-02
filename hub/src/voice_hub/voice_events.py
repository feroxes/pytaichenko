from typing import Protocol

from aioesphomeapi import VoiceAssistantEventType

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

Events = list[tuple[VoiceAssistantEventType, dict[str, str]]]


class DevicePort(Protocol):
    name: str
    volume: float

    def send_event(self, event: VoiceAssistantEventType, data: dict[str, str] | None = None) -> None: ...
    def set_volume(self, level: float) -> None: ...


def turn_started(url: str) -> Events:
    return [(RUN_START, {"url": url}), (STT_START, {})]


def speech_started() -> Events:
    return [(STT_VAD_START, {})]


def speech_stopped() -> Events:
    return [(STT_VAD_END, {})]


def answer_streaming() -> Events:
    return [(INTENT_PROGRESS, {"tts_start_streaming": "1"})]


def run_ended() -> Events:
    return [(RUN_END, {})]


def answered(conversation_id: str, transcript: list[str], url: str, streamed: bool, ended: bool) -> Events:
    text = " ".join(part.strip() for part in transcript if part.strip()) or "…"
    events: Events = [(INTENT_END, {"conversation_id": conversation_id})]
    if streamed:
        events.append((TTS_START, {"text": text}))
    elif not ended:
        events += [(TTS_START, {"text": text}), (TTS_END, {"url": url})]
    return events + run_ended()


def failed(conversation_id: str, code: str, message: str) -> Events:
    ending = [(INTENT_END, {"conversation_id": conversation_id}), (ERROR, {"code": code, "message": message})]
    return ending + run_ended()


def send(device: DevicePort, events: Events, follow_up: bool = False) -> None:
    for event, data in events:
        if event is INTENT_END:
            data = {**data, "continue_conversation": "1" if follow_up else "0"}
        device.send_event(event, data)
