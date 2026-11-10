import asyncio
import logging

import aiohttp
import pytest

from voice_hub.config import OpenAIConfig
from voice_hub.prompts import default_instructions
from voice_hub.realtime import (
    AudioDelta,
    Closed,
    InputCleared,
    InputTranscript,
    RealtimeError,
    RealtimeSession,
    ResponseCreated,
    ResponseDone,
    ServerError,
    SpeechStarted,
    SpeechStopped,
    ToolCall,
    TranscriptDone,
    parse_event,
    session_update,
)
from voice_hub.tools import session_tools

KEY = "sk-test"
INSTRUCTIONS = default_instructions("uk")


def cfg(url="wss://api.openai.com/v1/realtime", **kw):
    kw.setdefault("api_key", KEY)
    return OpenAIConfig(url=url, **kw)


@pytest.fixture
async def http():
    async with aiohttp.ClientSession() as session:
        yield session


@pytest.fixture
async def session(fake_openai, http):
    s = RealtimeSession(cfg(url=fake_openai.url), http)
    await s.connect()
    yield s
    await s.close()


def test_session_update_shape():
    assert session_update(cfg()) == {
        "type": "session.update",
        "session": {
            "type": "realtime",
            "output_modalities": ["audio"],
            "instructions": INSTRUCTIONS,
            "reasoning": {"effort": "low"},
            "audio": {
                "input": {
                    "format": {"type": "audio/pcm", "rate": 24000},
                    "noise_reduction": {"type": "far_field"},
                    "turn_detection": {
                        "type": "server_vad",
                        "threshold": 0.5,
                        "prefix_padding_ms": 300,
                        "silence_duration_ms": 600,
                        "create_response": True,
                        "interrupt_response": False,
                    },
                },
                "output": {"format": {"type": "audio/pcm", "rate": 24000}, "voice": "marin"},
            },
            "tools": session_tools("uk"),
            "tool_choice": "auto",
        },
    }


def test_session_update_takes_values_from_config():
    msg = session_update(cfg(voice="cedar", vad_threshold=0.8, silence_ms=900, instructions="Hi", reasoning_effort="high"))
    sess = msg["session"]
    assert sess["instructions"] == "Hi" and sess["reasoning"] == {"effort": "high"}
    assert sess["audio"]["output"]["voice"] == "cedar"
    td = sess["audio"]["input"]["turn_detection"]
    assert td["threshold"] == 0.8 and td["silence_duration_ms"] == 900


def test_session_update_with_transcription():
    msg = session_update(cfg(transcription_model="gpt-4o-mini-transcribe"))
    assert msg["session"]["audio"]["input"]["transcription"] == {"model": "gpt-4o-mini-transcribe", "language": "uk"}


def test_session_update_follows_the_language():
    sess = session_update(cfg(language="en", transcription_model="gpt-4o-mini-transcribe"))["session"]
    assert sess["audio"]["input"]["transcription"]["language"] == "en"
    assert sess["tools"] == session_tools("en")


def test_session_update_without_reasoning():
    assert "reasoning" not in session_update(cfg(reasoning_effort=None))["session"]


async def test_connect_sends_session_update(fake_openai, http):
    s = RealtimeSession(cfg(url=fake_openai.url), http)
    await s.connect()
    try:
        assert fake_openai.query["model"] == "gpt-realtime-2.1-mini"
        assert fake_openai.headers["Authorization"] == "Bearer sk-test"
        assert "OpenAI-Beta" not in fake_openai.headers
        assert len(fake_openai.received) == 1 and fake_openai.received[0]["type"] == "session.update"
        sess = fake_openai.received[0]["session"]
        assert sess["audio"]["input"]["format"] == {"type": "audio/pcm", "rate": 24000}
        td = sess["audio"]["input"]["turn_detection"]
        assert td["type"] == "server_vad" and td["silence_duration_ms"] == 600
        assert td["create_response"] is True and td["interrupt_response"] is False
        assert sess["audio"]["output"]["voice"] == "marin"
        assert [tool["name"] for tool in sess["tools"]] == ["set_volume", "end_conversation"]
        assert sess["instructions"] == INSTRUCTIONS and sess["reasoning"] == {"effort": "low"}
        assert s.closed is False
    finally:
        await s.close()
    assert s.closed is True


async def test_connect_logs_the_model_and_voice_openai_reports(fake_openai, http, caplog):
    caplog.set_level(logging.INFO, logger="voice_hub.realtime")
    s = RealtimeSession(cfg(url=fake_openai.url, model="gpt-realtime-2.1", voice="cedar"), http)
    await s.connect()
    await s.close()
    assert "realtime session ready: model gpt-realtime-2.1, voice cedar" in caplog.text


async def test_connect_without_a_key_fails_before_connecting(fake_openai, http):
    s = RealtimeSession(cfg(url=fake_openai.url, api_key=None), http)
    with pytest.raises(RealtimeError, match="key"):
        await s.connect()
    assert s.closed is True
    assert fake_openai.received == [] and "Authorization" not in fake_openai.headers


async def test_connect_fails_on_rejected_update(fake_openai, http):
    fake_openai.reject_update = True
    s = RealtimeSession(cfg(url=fake_openai.url), http)
    with pytest.raises(RealtimeError, match="bad session"):
        await s.connect()
    assert s.closed is True


async def test_connect_fails_on_http_401(fake_openai, http):
    fake_openai.status = 401
    s = RealtimeSession(cfg(url=fake_openai.url), http)
    with pytest.raises(RealtimeError) as info:
        await s.connect()
    assert "401" in str(info.value) and KEY not in str(info.value)
    assert s.closed is True


async def test_connect_times_out_without_session_created(fake_openai, http):
    fake_openai.send_created = False
    s = RealtimeSession(cfg(url=fake_openai.url), http)
    with pytest.raises(RealtimeError, match="timed out"):
        await s.connect(timeout=0.2)
    assert s.closed is True


async def test_connect_fails_when_server_drops_socket(fake_openai, http):
    fake_openai.drop_on_connect = True
    s = RealtimeSession(cfg(url=fake_openai.url), http)
    with pytest.raises(RealtimeError):
        await s.connect()
    assert s.closed is True


async def test_connect_fails_when_server_unreachable(http):
    s = RealtimeSession(cfg(url="ws://127.0.0.1:1/v1/realtime"), http)
    with pytest.raises(RealtimeError):
        await s.connect()
    assert s.closed is True


async def test_append_audio_base64(fake_openai, session):
    await session.append_audio(b"\x01\x00\x02\x00")
    await fake_openai.wait_received(2)
    assert fake_openai.received[1] == {"type": "input_audio_buffer.append", "audio": "AQACAA=="}


async def test_tool_outputs_then_single_response_create(fake_openai, session):
    await session.send_tool_outputs([("c1", '{"volume": 55}'), ("c2", "{}")])
    await fake_openai.wait_received(4)
    sent = fake_openai.received[1:]
    assert [m["type"] for m in sent] == [
        "conversation.item.create",
        "conversation.item.create",
        "response.create",
    ]
    assert sent[0]["item"] == {"type": "function_call_output", "call_id": "c1", "output": '{"volume": 55}'}
    assert sent[1]["item"] == {"type": "function_call_output", "call_id": "c2", "output": "{}"}


async def test_tool_outputs_without_response(fake_openai, session):
    await session.send_tool_outputs([("c1", '{"ok": true}')], respond=False)
    await session.send_tool_outputs([("c2", '{"volume": 40}')], respond=True)
    await fake_openai.wait_received(4)
    sent = fake_openai.received[1:]
    assert [m["type"] for m in sent] == ["conversation.item.create", "conversation.item.create", "response.create"]
    assert sent[0]["item"] == {"type": "function_call_output", "call_id": "c1", "output": '{"ok": true}'}
    assert sent[1]["item"] == {"type": "function_call_output", "call_id": "c2", "output": '{"volume": 40}'}


async def test_empty_tool_outputs_send_nothing(fake_openai, session):
    await session.send_tool_outputs([])
    await session.clear_input()
    await fake_openai.wait_received(2)
    assert [m["type"] for m in fake_openai.received[1:]] == ["input_audio_buffer.clear"]


async def test_commit_cancel_clear_messages(fake_openai, session):
    await session.clear_input()
    await session.commit_input()
    await session.cancel_response()
    await fake_openai.wait_received(5)
    assert [m["type"] for m in fake_openai.received[1:]] == [
        "input_audio_buffer.clear",
        "input_audio_buffer.commit",
        "response.create",
        "response.cancel",
    ]


async def test_next_event_returns_pushed_events_in_order(fake_openai, session):
    await fake_openai.push({"type": "input_audio_buffer.speech_started"})
    await fake_openai.push({"type": "input_audio_buffer.speech_stopped"})
    await fake_openai.push({"type": "response.created", "response": {"id": "r1"}})
    assert isinstance(await asyncio.wait_for(session.next_event(), 2), SpeechStarted)
    assert isinstance(await asyncio.wait_for(session.next_event(), 2), SpeechStopped)
    assert await asyncio.wait_for(session.next_event(), 2) == ResponseCreated("r1")


async def test_next_event_skips_unknown_and_malformed(fake_openai, session):
    await fake_openai.push({"type": "rate_limits.updated", "rate_limits": []})
    await fake_openai.push({"type": "response.created"})
    await fake_openai.push({"type": "response.output_audio.delta", "response_id": "r1", "delta": "!!!"})
    await fake_openai.push({"type": "input_audio_buffer.speech_started"})
    assert isinstance(await asyncio.wait_for(session.next_event(), 2), SpeechStarted)


async def test_next_event_closed_when_server_closes(fake_openai, session):
    await fake_openai.close_connection()
    assert isinstance(await asyncio.wait_for(session.next_event(), 2), Closed)
    assert session.closed is True
    assert isinstance(await asyncio.wait_for(session.next_event(), 2), Closed)


async def test_send_after_server_close_raises_realtime_error(fake_openai, session):
    await fake_openai.close_connection()
    assert isinstance(await asyncio.wait_for(session.next_event(), 2), Closed)
    with pytest.raises(RealtimeError):
        await session.append_audio(b"\x00\x00")


async def test_close_is_idempotent_and_blocks_sends(fake_openai, http):
    s = RealtimeSession(cfg(url=fake_openai.url), http)
    await s.connect()
    await s.close()
    await s.close()
    assert s.closed is True
    assert isinstance(await s.next_event(), Closed)
    with pytest.raises(RealtimeError):
        await s.commit_input()


async def test_send_before_connect_raises_realtime_error(http):
    s = RealtimeSession(cfg(), http)
    with pytest.raises(RealtimeError):
        await s.append_audio(b"\x00\x00")


CALL = {"type": "function_call", "call_id": "call_1", "name": "set_volume", "arguments": '{"action":"down"}'}
USAGE = {"total_tokens": 10, "input_tokens": 6, "output_tokens": 4}


@pytest.mark.parametrize(
    ("msg", "expected"),
    [
        pytest.param({"type": "input_audio_buffer.speech_started"}, SpeechStarted(), id="speech_started"),
        pytest.param({"type": "input_audio_buffer.speech_stopped"}, SpeechStopped(), id="speech_stopped"),
        pytest.param({"type": "input_audio_buffer.cleared", "event_id": "e1"}, InputCleared(), id="input_cleared"),
        pytest.param(
            {"type": "response.created", "response": {"id": "r1", "status": "in_progress"}},
            ResponseCreated("r1"),
            id="response_created",
        ),
        pytest.param(
            {"type": "response.output_audio.delta", "response_id": "r1", "delta": "AQACAA=="},
            AudioDelta("r1", b"\x01\x00\x02\x00"),
            id="audio_delta",
        ),
        pytest.param(
            {"type": "response.output_audio_transcript.done", "response_id": "r1", "transcript": "Привіт"},
            TranscriptDone("r1", "Привіт"),
            id="transcript_done",
        ),
        pytest.param(
            {
                "type": "response.done",
                "response": {
                    "id": "r1",
                    "status": "completed",
                    "status_details": None,
                    "output": [{"type": "message", "role": "assistant", "content": []}],
                    "usage": USAGE,
                },
            },
            ResponseDone("r1", "completed", (), None, USAGE),
            id="response_done_completed",
        ),
        pytest.param(
            {"type": "response.done", "response": {"id": "r2", "status": "cancelled", "output": []}},
            ResponseDone("r2", "cancelled", (), None, {}),
            id="response_done_without_usage",
        ),
        pytest.param(
            {
                "type": "response.done",
                "response": {"id": "r3", "status": "completed", "output": [CALL], "usage": USAGE},
            },
            ResponseDone(
                "r3", "completed", (ToolCall("call_1", "set_volume", '{"action":"down"}'),), None, USAGE
            ),
            id="response_done_tool_call",
        ),
        pytest.param(
            {
                "type": "response.done",
                "response": {
                    "id": "r4",
                    "status": "failed",
                    "status_details": {
                        "type": "failed",
                        "error": {"type": "server_error", "code": "insufficient_quota", "message": "no quota"},
                    },
                    "output": [],
                    "usage": None,
                },
            },
            ResponseDone("r4", "failed", (), "insufficient_quota", {}),
            id="response_done_failed",
        ),
        pytest.param(
            {
                "type": "error",
                "error": {"type": "invalid_request_error", "code": "rate_limit_exceeded", "message": "slow down"},
            },
            ServerError("rate_limit_exceeded", "slow down"),
            id="error_with_code",
        ),
        pytest.param(
            {"type": "error", "error": {"type": "invalid_request_error", "code": None, "message": "bad"}},
            ServerError("invalid_request_error", "bad"),
            id="error_without_code",
        ),
        pytest.param(
            {
                "type": "conversation.item.input_audio_transcription.completed",
                "item_id": "i1",
                "content_index": 0,
                "transcript": "Яка столиця Франції?",
            },
            InputTranscript("Яка столиця Франції?"),
            id="input_transcript",
        ),
        pytest.param({"type": "session.updated", "session": {}}, None, id="session_updated_ignored"),
        pytest.param({"type": "rate_limits.updated"}, None, id="unknown_type"),
        pytest.param({}, None, id="no_type"),
    ],
)
def test_parse_event(msg, expected):
    assert parse_event(msg) == expected
