import asyncio
import base64
import json
import logging
from dataclasses import dataclass
from typing import Any

import aiohttp

from voice_hub.config import OpenAIConfig
from voice_hub.tools import session_tools

log = logging.getLogger(__name__)

_CLOSED_TYPES = (aiohttp.WSMsgType.CLOSE, aiohttp.WSMsgType.CLOSING, aiohttp.WSMsgType.CLOSED, aiohttp.WSMsgType.ERROR)


class RealtimeError(Exception):
    pass


@dataclass(frozen=True)
class SpeechStarted:
    pass


@dataclass(frozen=True)
class SpeechStopped:
    pass


@dataclass(frozen=True)
class InputCleared:
    pass


@dataclass(frozen=True)
class ResponseCreated:
    response_id: str


@dataclass(frozen=True)
class AudioDelta:
    response_id: str
    pcm: bytes


@dataclass(frozen=True)
class TranscriptDone:
    response_id: str
    text: str


@dataclass(frozen=True)
class ToolCall:
    call_id: str
    name: str
    arguments: str


@dataclass(frozen=True)
class ResponseDone:
    response_id: str
    status: str
    tool_calls: tuple[ToolCall, ...]
    error: str | None
    usage: dict


@dataclass(frozen=True)
class InputTranscript:
    text: str


@dataclass(frozen=True)
class ServerError:
    code: str
    message: str


@dataclass(frozen=True)
class Closed:
    pass


RealtimeEvent = (
    SpeechStarted | SpeechStopped | InputCleared | ResponseCreated | AudioDelta | TranscriptDone | ResponseDone
    | InputTranscript | ServerError | Closed
)


def session_update(cfg: OpenAIConfig) -> dict:
    session: dict[str, Any] = {
        "type": "realtime",
        "output_modalities": ["audio"],
        "instructions": cfg.instructions,
    }
    if cfg.reasoning_effort is not None:
        session["reasoning"] = {"effort": cfg.reasoning_effort}
    session["audio"] = {
        "input": {
            "format": {"type": "audio/pcm", "rate": 24000},
            "noise_reduction": {"type": "far_field"},
            "turn_detection": {
                "type": "server_vad",
                "threshold": cfg.vad_threshold,
                "prefix_padding_ms": 300,
                "silence_duration_ms": cfg.silence_ms,
                "create_response": True,
                "interrupt_response": False,
            },
        },
        "output": {"format": {"type": "audio/pcm", "rate": 24000}, "voice": cfg.voice},
    }
    if cfg.transcription_model is not None:
        session["audio"]["input"]["transcription"] = {"model": cfg.transcription_model, "language": cfg.language}
    session["tools"] = session_tools(cfg.language)
    session["tool_choice"] = "auto"
    return {"type": "session.update", "session": session}


def _response_done(response: dict) -> ResponseDone:
    details = response.get("status_details") or {}
    error = details.get("error") or {}
    calls = tuple(
        ToolCall(item["call_id"], item["name"], item.get("arguments") or "")
        for item in response.get("output") or []
        if item.get("type") == "function_call"
    )
    return ResponseDone(
        response_id=response["id"],
        status=response["status"],
        tool_calls=calls,
        error=error.get("code") or error.get("type"),
        usage=response.get("usage") or {},
    )


def parse_event(msg: dict) -> RealtimeEvent | None:
    match msg.get("type"):
        case "input_audio_buffer.speech_started":
            return SpeechStarted()
        case "input_audio_buffer.speech_stopped":
            return SpeechStopped()
        case "input_audio_buffer.cleared":
            return InputCleared()
        case "response.created":
            return ResponseCreated(msg["response"]["id"])
        case "response.output_audio.delta":
            return AudioDelta(msg["response_id"], base64.b64decode(msg["delta"], validate=True))
        case "response.output_audio_transcript.done":
            return TranscriptDone(msg["response_id"], msg["transcript"])
        case "response.done":
            return _response_done(msg["response"])
        case "conversation.item.input_audio_transcription.completed":
            return InputTranscript(msg["transcript"])
        case "error":
            error = msg.get("error") or {}
            return ServerError(error.get("code") or error.get("type") or "unknown", error.get("message") or "")
    return None


class RealtimeSession:
    def __init__(self, cfg: OpenAIConfig, http: aiohttp.ClientSession) -> None:
        self._cfg = cfg
        self._http = http
        self._ws: aiohttp.ClientWebSocketResponse | None = None
        self.closed = False

    async def connect(self, timeout: float = 10.0) -> None:
        try:
            async with asyncio.timeout(timeout):
                await self._open()
        except TimeoutError as e:
            await self.close()
            raise RealtimeError(f"realtime connect timed out after {timeout:g}s") from e
        except aiohttp.WSServerHandshakeError as e:
            await self.close()
            raise RealtimeError(f"realtime handshake rejected: HTTP {e.status}") from e
        except aiohttp.ClientError as e:
            await self.close()
            raise RealtimeError(f"realtime connection failed: {type(e).__name__}") from e
        except BaseException:
            await self.close()
            raise

    async def _open(self) -> None:
        if not self._cfg.api_key:
            raise RealtimeError("the OpenAI API key is not set")
        url = f"{self._cfg.url}?model={self._cfg.model}"
        headers = {"Authorization": f"Bearer {self._cfg.api_key}"}
        self._ws = await self._http.ws_connect(url, headers=headers, heartbeat=15)
        created = await self._expect("session.created")
        await self._send(session_update(self._cfg))
        updated = await self._expect("session.updated")
        model = created.get("model")
        voice = ((updated.get("audio") or {}).get("output") or {}).get("voice")
        log.info("realtime session ready: model %s, voice %s", model or "?", voice or "?")

    async def _expect(self, expected: str) -> dict:
        assert self._ws is not None
        while True:
            frame = await self._ws.receive()
            if frame.type == aiohttp.WSMsgType.TEXT:
                msg = _decode(frame.data)
                if msg is None:
                    continue
                kind = msg.get("type")
                if kind == "error":
                    error = msg.get("error") or {}
                    code = error.get("code") or error.get("type") or "unknown"
                    raise RealtimeError(f"realtime error {code}: {error.get('message') or ''}")
                if kind == expected:
                    return msg.get("session") or {}
            elif frame.type in _CLOSED_TYPES:
                raise RealtimeError(f"realtime socket closed before {expected}")

    async def _send(self, msg: dict) -> None:
        if self._ws is None or self.closed:
            raise RealtimeError("realtime session is not open")
        try:
            await self._ws.send_json(msg)
        except (ConnectionError, aiohttp.ClientError) as e:
            self.closed = True
            raise RealtimeError(f"realtime send failed: {type(e).__name__}") from e

    async def append_audio(self, pcm24k: bytes) -> None:
        await self._send({"type": "input_audio_buffer.append", "audio": base64.b64encode(pcm24k).decode("ascii")})

    async def clear_input(self) -> None:
        await self._send({"type": "input_audio_buffer.clear"})

    async def commit_input(self) -> None:
        await self._send({"type": "input_audio_buffer.commit"})
        await self._send({"type": "response.create"})

    async def cancel_response(self) -> None:
        await self._send({"type": "response.cancel"})

    async def send_tool_outputs(self, outputs: list[tuple[str, str]], respond: bool = True) -> None:
        if not outputs:
            return
        for call_id, output in outputs:
            await self._send(
                {
                    "type": "conversation.item.create",
                    "item": {"type": "function_call_output", "call_id": call_id, "output": output},
                }
            )
        if respond:
            await self._send({"type": "response.create"})

    async def next_event(self) -> RealtimeEvent:
        while True:
            if self._ws is None or self.closed:
                return Closed()
            frame = await self._ws.receive()
            if frame.type == aiohttp.WSMsgType.TEXT:
                msg = _decode(frame.data)
                if msg is None:
                    continue
                try:
                    event = parse_event(msg)
                except (KeyError, TypeError, ValueError, AttributeError):
                    log.warning("skipping malformed realtime event %r", msg.get("type"))
                    continue
                if event is not None:
                    return event
            elif frame.type in _CLOSED_TYPES:
                self.closed = True
                return Closed()

    async def close(self) -> None:
        self.closed = True
        ws = self._ws
        if ws is not None and not ws.closed:
            await ws.close()


def _decode(data: str) -> dict | None:
    try:
        msg = json.loads(data)
    except ValueError:
        return None
    return msg if isinstance(msg, dict) else None
