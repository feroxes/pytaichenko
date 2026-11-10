import asyncio
import json
import re
import time
from dataclasses import replace

import numpy as np
import pytest
from aiohttp import web
from aiohttp.test_utils import make_mocked_request

from fakes import (
    SETTINGS,
    TIMING,
    E,
    FakeDevice,
    FakeSession,
    FakeStreams,
    SessionFactory,
    audio_url,
    wait_for,
)
from voice_hub.audio import HighPass, Loudness, Resampler
from voice_hub.conversation import Conversation
from voice_hub.realtime import (
    AudioDelta,
    Closed,
    InputTranscript,
    ResponseCreated,
    ResponseDone,
    SpeechStarted,
    SpeechStopped,
    ToolCall,
    TranscriptDone,
)

CHUNK = b"\x00\x00" * 512
SPEECH = (SpeechStarted(), SpeechStopped())
HAPPY = [
    E.RUN_START,
    E.STT_START,
    E.STT_VAD_START,
    E.STT_VAD_END,
    E.INTENT_PROGRESS,
    E.INTENT_END,
    E.TTS_START,
    E.RUN_END,
]


def pcm_matches(actual: bytes, expected: bytes, tolerance: int = 4) -> bool:
    if len(actual) != len(expected):
        return False
    a = np.frombuffer(actual, "<i2").astype(np.int32)
    b = np.frombuffer(expected, "<i2").astype(np.int32)
    return bool(np.all(np.abs(a - b) <= tolerance))


def answer(rid, text="Так."):
    return [
        ResponseCreated(rid),
        AudioDelta(rid, b"\x00\x00" * 2400),
        TranscriptDone(rid, text),
        ResponseDone(rid, "completed", (), None, {}),
    ]


async def begin(conv, session):
    clears = session.calls.count("clear")
    assert await conv.handle_start("", 0, SETTINGS, "Hey Jarvis") == 0
    await wait_for(lambda: session.calls.count("clear") > clears)


async def drain(stream):
    data = bytearray()
    while (chunk := await asyncio.wait_for(stream.read(), 1.0)) is not None:
        data += chunk
    return bytes(data)


@pytest.fixture
def device():
    return FakeDevice()


@pytest.fixture
def session():
    return FakeSession()


@pytest.fixture
def factory(session):
    return SessionFactory(session)


@pytest.fixture
def streams():
    return FakeStreams()


@pytest.fixture
async def make_conv(device, streams, factory):
    made = []

    def make(debug_input=None, **timing):
        conv = Conversation(device, streams, factory, replace(TIMING, **timing), audio_url, debug_input)
        made.append(conv)
        return conv

    yield make
    for conv in made:
        await conv.close()


@pytest.fixture
def conv(make_conv):
    return make_conv()


async def test_happy_turn_event_order(conv, device, session, streams):
    assert await conv.handle_start("", 0, SETTINGS, "Hey Jarvis") == 0
    for _ in range(2):
        await conv.handle_audio(CHUNK)
    await wait_for(lambda: len(session.appended) > 0)
    session.push(
        SpeechStarted(),
        SpeechStopped(),
        ResponseCreated("r1"),
        AudioDelta("r1", b"\x00\x00" * 2400),
        TranscriptDone("r1", "П'ятдесят шість."),
        ResponseDone("r1", "completed", (), None, {}),
    )
    await wait_for(lambda: E.RUN_END in device.types())
    assert device.types() == HAPPY
    url = device.events[0][1]["url"]
    assert url.startswith("http://hub:8080/audio/") and url.endswith(".wav")
    assert url == audio_url(streams.created[0].id)
    assert device.events[1][1] == {}
    assert device.events[4][1] == {"tts_start_streaming": "1"}
    assert device.events[5][1]["continue_conversation"] == "1" and device.events[5][1]["conversation_id"]
    assert device.events[6][1] == {"text": "П'ятдесят шість."}
    assert device.events[7][1] == {}
    assert session.calls[0] == "clear"
    assert streams.created[0].finished
    assert len(await drain(streams.created[0])) == 3200


async def test_answer_audio_is_boosted_for_the_speaker(conv, device, session, streams):
    await begin(conv, session)
    t = np.arange(2400) / 24000
    tone = (np.sin(2 * np.pi * 1000 * t) * 8000).astype("<i2").tobytes()
    session.push(*SPEECH, ResponseCreated("r1"), AudioDelta("r1", tone), ResponseDone("r1", "completed", (), None, {}))
    await wait_for(lambda: E.RUN_END in device.types())
    resampler, boost = Resampler(24000, 16000), Loudness(16000)
    assert await drain(streams.created[0]) == boost.process(resampler.process(tone)) + boost.process(resampler.flush())


async def test_events_wait_for_mic_stop_grace(conv, device, session):
    await begin(conv, session)
    session.push(*SPEECH, *answer("r1"))
    await wait_for(lambda: E.RUN_END in device.types())
    vad_end = device.times_of(E.STT_VAD_END)[0]
    assert device.times_of(E.INTENT_PROGRESS)[0] - vad_end >= 0.05

    await begin(conv, session)
    session.push(*SPEECH, ResponseCreated("r2"), TranscriptDone("r2", "Так."), ResponseDone("r2", "completed", (), None, {}))
    await wait_for(lambda: device.types().count(E.RUN_END) == 2)
    second = device.types()[8:]
    assert second == [E.RUN_START, E.STT_START, E.STT_VAD_START, E.STT_VAD_END, E.INTENT_END, E.TTS_START, E.TTS_END, E.RUN_END]
    vad_end = device.times_of(E.STT_VAD_END)[1]
    assert all(t - vad_end >= 0.05 for t in device.times[12:])


async def test_no_tts_end_after_streaming(conv, device, session):
    await begin(conv, session)
    session.push(*SPEECH, *answer("r1"))
    await wait_for(lambda: E.RUN_END in device.types())
    assert device.types() == [
        E.RUN_START,
        E.STT_START,
        E.STT_VAD_START,
        E.STT_VAD_END,
        E.INTENT_PROGRESS,
        E.INTENT_END,
        E.TTS_START,
        E.RUN_END,
    ]
    assert E.TTS_END not in device.types()


async def test_tts_end_when_nothing_streamed(conv, device, session, streams):
    await begin(conv, session)
    session.push(*SPEECH, ResponseCreated("r1"), TranscriptDone("r1", "Так."), ResponseDone("r1", "completed", (), None, {}))
    await wait_for(lambda: E.RUN_END in device.types())
    assert E.INTENT_PROGRESS not in device.types()
    assert device.types()[-4:] == [E.INTENT_END, E.TTS_START, E.TTS_END, E.RUN_END]
    assert device.events[-2][1] == {"url": audio_url(streams.created[0].id)}


async def test_audio_buffered_until_session_ready(conv, session):
    session.connect_delay = 0.2
    wave = (np.sin(np.arange(1536) * 2 * np.pi * 440 / 16000) * 8000).astype("<i2").tobytes()
    chunks = [wave[i : i + 1024] for i in range(0, len(wave), 1024)]
    assert await conv.handle_start("", 0, SETTINGS, "Hey Jarvis") == 0
    for chunk in chunks:
        await conv.handle_audio(chunk)
    await asyncio.sleep(0.05)
    assert len(session.appended) == 0 and not session.connected
    resampler = Resampler(16000, 24000)
    high_pass = HighPass(16000)
    expected = b"".join(resampler.process(high_pass.process(chunk)) for chunk in chunks)
    await wait_for(lambda: len(session.appended) >= len(expected))
    assert pcm_matches(bytes(session.appended), expected)
    assert session.trace[:2] == ["connect", "clear"]


async def test_audio_after_speech_stopped_not_sent(conv, device, session):
    await begin(conv, session)
    for _ in range(3):
        await conv.handle_audio(CHUNK)
    await wait_for(lambda: len(session.appended) > 0)
    session.push(*SPEECH)
    await wait_for(lambda: E.STT_VAD_END in device.types())
    sent = len(session.appended)
    for _ in range(5):
        await conv.handle_audio(CHUNK)
    await asyncio.sleep(0.05)
    assert len(session.appended) == sent


async def test_first_turn_no_speech_timeout(make_conv, device, session, streams):
    conv = make_conv(first_turn_s=0.2)
    started = time.monotonic()
    await begin(conv, session)
    await wait_for(lambda: E.RUN_END in device.types())
    assert device.types() == [E.RUN_START, E.STT_START, E.RUN_END]
    assert 0.2 <= device.times[-1] - started < 0.6
    assert session.calls.count("clear") == 2
    assert E.ERROR not in device.types()
    assert streams.created[0].finished
    assert not session.closed


async def test_heard_speech_is_logged(conv, session, caplog):
    caplog.set_level("INFO", logger="voice_hub.conversation")
    await begin(conv, session)
    session.push(InputTranscript("Яка столиця Франції?"))
    await wait_for(lambda: any(r.getMessage() == "heard on kitchen: Яка столиця Франції?" for r in caplog.records))


async def test_turn_end_logs_device_audio_level(make_conv, device, session, caplog):
    caplog.set_level("INFO", logger="voice_hub.conversation")
    conv = make_conv(first_turn_s=0.2)
    await begin(conv, session)
    t = np.arange(512 * 31) / 16000
    tone = (np.sin(2 * np.pi * 1000 * t) * 2000).astype(np.int16).tobytes()
    for i in range(0, len(tone), 1024):
        await conv.handle_audio(tone[i : i + 1024])
    await wait_for(lambda: E.RUN_END in device.types())
    line = next(r.getMessage() for r in caplog.records if r.getMessage().startswith("device audio on"))
    match = re.search(r": ([0-9.]+) s, peak ([0-9]+) \((-[0-9.]+) dBFS\)", line)
    assert match is not None
    assert match.group(1) == "0.99"
    assert 1900 <= int(match.group(2)) <= 2200
    assert -25.0 <= float(match.group(3)) <= -23.4


async def test_device_audio_in_holdoff_is_not_sent(make_conv, device, session):
    conv = make_conv(mic_holdoff_s=0.064)
    await begin(conv, session)
    loud = np.full(512, 30000, dtype=np.int16).tobytes()
    quiet = np.full(512, 100, dtype=np.int16).tobytes()
    await conv.handle_audio(loud)
    await conv.handle_audio(loud)
    for _ in range(4):
        await conv.handle_audio(quiet)
    await asyncio.sleep(0.05)
    sent = np.frombuffer(bytes(session.appended), dtype=np.int16)
    assert sent.size > 0
    assert int(np.abs(sent.astype(np.int32)).max()) < 1000


async def test_a_new_holdoff_applies_from_the_next_question(device, streams, factory, session):
    current = [TIMING]
    conv = Conversation(device, streams, factory, lambda: current[0], audio_url)
    try:
        await begin(conv, session)
        session.push(*SPEECH, *answer("r1"))
        await wait_for(lambda: E.RUN_END in device.types())
        current[0] = replace(TIMING, mic_holdoff_s=0.064)
        second = FakeSession()
        factory.sessions.append(second)
        await begin(conv, second)
        loud = np.full(512, 30000, dtype=np.int16).tobytes()
        quiet = np.full(512, 100, dtype=np.int16).tobytes()
        await conv.handle_audio(loud)
        await conv.handle_audio(loud)
        for _ in range(4):
            await conv.handle_audio(quiet)
        await wait_for(lambda: len(second.appended) > 0)
        await asyncio.sleep(0.05)
        sent = np.frombuffer(bytes(second.appended), dtype=np.int16)
        assert int(np.abs(sent.astype(np.int32)).max()) < 1000
    finally:
        await conv.close()


async def test_device_audio_is_high_passed_before_sending(make_conv, device, session):
    conv = make_conv()
    await begin(conv, session)
    t = np.arange(16000) / 16000
    drift = (np.sin(2 * np.pi * 3 * t) * 20000).astype(np.int16).tobytes()
    for i in range(0, len(drift), 1024):
        await conv.handle_audio(drift[i : i + 1024])
    await asyncio.sleep(0.05)
    sent = np.frombuffer(bytes(session.appended), dtype=np.int16).astype(np.int32)
    assert sent.size > 0
    assert np.abs(sent[len(sent) // 2 :]).max() < 200


async def test_events_before_turn_ready_are_ignored(conv, device, session):
    await begin(conv, session)
    session.push(*SPEECH, *answer("r1"))
    await wait_for(lambda: E.RUN_END in device.types())
    assert await conv.handle_start("", 0, SETTINGS, None) == 0
    session.push(SpeechStarted())
    await wait_for(lambda: device.types().count(E.RUN_END) == 2)
    assert device.types()[8:] == [E.RUN_START, E.STT_START, E.RUN_END]


async def test_follow_up_uses_short_timeout(conv, device, session):
    await begin(conv, session)
    session.push(*SPEECH, *answer("r1"))
    await wait_for(lambda: E.RUN_END in device.types())
    started = time.monotonic()
    await begin(conv, session)
    await wait_for(lambda: device.types().count(E.RUN_END) == 2)
    assert 0.3 <= device.times[-1] - started <= 0.8
    assert device.types()[8:] == [E.RUN_START, E.STT_START, E.RUN_END]


async def test_follow_ups_are_capped(make_conv, device, session):
    conv = make_conv(max_follow_ups=2)
    for count in (1, 2, 3):
        await begin(conv, session)
        session.push(*SPEECH, *answer(f"r{count}"))
        await wait_for(lambda: device.types().count(E.RUN_END) == count)
    flags = [data["continue_conversation"] for event, data in device.events if event == E.INTENT_END]
    assert flags == ["1", "1", "0"]
    started = time.monotonic()
    await begin(conv, session)
    await wait_for(lambda: device.types().count(E.RUN_END) == 4, timeout=2.0)
    assert 0.9 <= device.times[-1] - started <= 1.5
    assert device.types()[-3:] == [E.RUN_START, E.STT_START, E.RUN_END]


async def test_max_speech_forces_answer(make_conv, device, session):
    conv = make_conv(max_speech_s=0.2)
    await begin(conv, session)
    session.push(SpeechStarted())
    await wait_for(lambda: E.STT_VAD_END in device.types())
    assert device.times_of(E.STT_VAD_END)[0] - device.times_of(E.STT_VAD_START)[0] >= 0.19
    assert "commit" in session.calls
    session.push(*answer("r1"))
    await wait_for(lambda: E.RUN_END in device.types())
    assert device.types() == HAPPY


async def test_volume_tool_call(conv, device, session):
    await begin(conv, session)
    call = ToolCall("c1", "set_volume", '{"action":"down"}')
    session.push(*SPEECH, ResponseCreated("r1"), ResponseDone("r1", "completed", (call,), None, {}))
    await wait_for(lambda: any(isinstance(c, tuple) for c in session.calls))
    assert device.volume_calls == [0.55]
    assert ("tools", [("c1", '{"volume": 55}')], True) in session.calls
    assert E.INTENT_END not in device.types() and E.RUN_END not in device.types()
    session.push(*answer("r2", "Зробив тихіше."))
    await wait_for(lambda: E.RUN_END in device.types())
    assert device.types() == HAPPY
    assert device.events[6][1] == {"text": "Зробив тихіше."}


async def test_bad_tool_calls_return_errors(conv, device, session):
    await begin(conv, session)
    calls = (
        ToolCall("c1", "set_volume", '{"action":"sideways"}'),
        ToolCall("c2", "open_door", "{}"),
        ToolCall("c3", "set_volume", "not json"),
    )
    session.push(*SPEECH, ResponseCreated("r1"), ResponseDone("r1", "completed", calls, None, {}))
    await wait_for(lambda: any(isinstance(c, tuple) for c in session.calls))
    tools = [c for c in session.calls if isinstance(c, tuple)]
    assert len(tools) == 1
    outputs = tools[0][1]
    assert [call_id for call_id, _ in outputs] == ["c1", "c2", "c3"]
    assert all(json.loads(output)["error"] for _, output in outputs)
    assert device.volume_calls == []


END = ToolCall("e1", "end_conversation", "{}")


def tool_calls(session):
    return [c for c in session.calls if isinstance(c, tuple)]


async def test_end_conversation_without_audio(make_conv, device, session, streams, factory):
    conv = make_conv(response_timeout_s=0.3, session_idle_s=5.0)
    await begin(conv, session)
    session.push(*SPEECH, ResponseCreated("r1"), ResponseDone("r1", "completed", (END,), None, {}))
    await wait_for(lambda: E.RUN_END in device.types())
    assert device.types() == [E.RUN_START, E.STT_START, E.STT_VAD_START, E.STT_VAD_END, E.INTENT_END, E.RUN_END]
    assert device.events[4][1]["continue_conversation"] == "0" and device.events[4][1]["conversation_id"]
    assert device.times_of(E.INTENT_END)[0] - device.times_of(E.STT_VAD_END)[0] >= 0.05
    assert tool_calls(session) == [("tools", [("e1", '{"ok": true}')], False)]
    assert streams.created[0].finished
    await asyncio.sleep(0.4)
    assert device.types().count(E.RUN_END) == 1 and E.ERROR not in device.types()
    assert not session.closed
    started = time.monotonic()
    await begin(conv, session)
    await wait_for(lambda: device.types().count(E.RUN_END) == 2, timeout=2.0)
    assert 0.9 <= device.times[-1] - started <= 1.5
    assert device.types()[6:] == [E.RUN_START, E.STT_START, E.RUN_END]
    assert factory.calls == 1


async def test_end_conversation_after_goodbye_audio(conv, device, session, streams):
    await begin(conv, session)
    session.push(
        *SPEECH,
        ResponseCreated("r1"),
        AudioDelta("r1", b"\x00\x00" * 2400),
        TranscriptDone("r1", "До зустрічі!"),
        ResponseDone("r1", "completed", (END,), None, {}),
    )
    await wait_for(lambda: E.RUN_END in device.types())
    assert device.types() == HAPPY
    assert device.events[5][1]["continue_conversation"] == "0"
    assert device.events[6][1] == {"text": "До зустрічі!"}
    assert E.TTS_END not in device.types()
    assert tool_calls(session) == [("tools", [("e1", '{"ok": true}')], False)]
    assert len(await drain(streams.created[0])) == 3200


async def test_end_conversation_with_volume(make_conv, device, session):
    conv = make_conv(response_timeout_s=0.3)
    await begin(conv, session)
    calls = (ToolCall("c1", "set_volume", '{"action":"down"}'), END)
    session.push(*SPEECH, ResponseCreated("r1"), ResponseDone("r1", "completed", calls, None, {}))
    await wait_for(lambda: E.RUN_END in device.types())
    assert device.volume_calls == [0.55]
    assert tool_calls(session) == [("tools", [("c1", '{"volume": 55}'), ("e1", '{"ok": true}')], False)]
    assert device.types() == [E.RUN_START, E.STT_START, E.STT_VAD_START, E.STT_VAD_END, E.INTENT_END, E.RUN_END]
    assert device.events[-2][1]["continue_conversation"] == "0"
    await asyncio.sleep(0.4)
    assert device.types().count(E.RUN_END) == 1 and E.ERROR not in device.types()


async def test_end_conversation_resets_follow_up_count(make_conv, device, session):
    conv = make_conv(max_follow_ups=2)
    ending = [ResponseCreated("r2"), ResponseDone("r2", "completed", (END,), None, {})]
    turns = [answer("r1"), ending, answer("r3"), answer("r4"), answer("r5")]
    for number, events in enumerate(turns, 1):
        await begin(conv, session)
        session.push(*SPEECH, *events)
        await wait_for(lambda count=number: device.types().count(E.RUN_END) == count)
    flags = [data["continue_conversation"] for event, data in device.events if event == E.INTENT_END]
    assert flags == ["1", "0", "1", "1", "0"]


async def test_connect_failure_reports_error(conv, device, session, streams):
    session.fail_connect = True
    assert await conv.handle_start("", 0, SETTINGS, "Hey Jarvis") == 0
    await wait_for(lambda: E.RUN_END in device.types())
    assert device.types() == [E.RUN_START, E.STT_START, E.INTENT_END, E.ERROR, E.RUN_END]
    assert device.events[2][1]["continue_conversation"] == "0" and device.events[2][1]["conversation_id"]
    assert device.events[3][1]["code"] == "openai-unavailable" and device.events[3][1]["message"]
    assert streams.created[0].finished


async def test_slow_connect_failure_reports_error(make_conv, device, session, streams):
    conv = make_conv(first_turn_s=0.1)
    session.connect_delay = 0.3
    session.fail_connect = True
    assert await conv.handle_start("", 0, SETTINGS, "Hey Jarvis") == 0
    await wait_for(lambda: E.RUN_END in device.types())
    assert device.types() == [E.RUN_START, E.STT_START, E.INTENT_END, E.ERROR, E.RUN_END]
    assert device.events[2][1]["continue_conversation"] == "0"
    assert device.events[3][1]["code"] == "openai-unavailable"
    assert streams.created[0].finished


async def test_no_speech_timer_starts_when_ready(make_conv, device, session):
    conv = make_conv(first_turn_s=0.2)
    session.connect_delay = 0.3
    assert await conv.handle_start("", 0, SETTINGS, "Hey Jarvis") == 0
    await asyncio.sleep(0.25)
    assert device.types() == [E.RUN_START, E.STT_START]
    await wait_for(lambda: "clear" in session.calls)
    ready = time.monotonic()
    assert device.types() == [E.RUN_START, E.STT_START]
    await wait_for(lambda: E.RUN_END in device.types())
    assert device.types() == [E.RUN_START, E.STT_START, E.RUN_END]
    assert 0.19 <= device.times[-1] - ready < 0.45
    assert session.calls == ["clear", "clear"]


async def test_failed_response_reports_error(conv, device, session, factory):
    await begin(conv, session)
    session.push(*SPEECH, ResponseCreated("r1"), ResponseDone("r1", "failed", (), "insufficient_quota", {}))
    await wait_for(lambda: E.RUN_END in device.types())
    assert device.types() == [
        E.RUN_START,
        E.STT_START,
        E.STT_VAD_START,
        E.STT_VAD_END,
        E.INTENT_END,
        E.ERROR,
        E.RUN_END,
    ]
    assert device.events[4][1]["continue_conversation"] == "0"
    assert device.events[5][1]["code"] == "openai-error" and device.events[5][1]["message"]
    assert session.closed and session.close_calls == 1
    second = FakeSession()
    factory.sessions.append(second)
    await begin(conv, second)
    assert factory.calls == 2


async def test_session_closed_mid_turn(conv, device, session, streams):
    await begin(conv, session)
    session.push(*SPEECH, Closed())
    await wait_for(lambda: E.RUN_END in device.types())
    assert device.types()[4:] == [E.INTENT_END, E.ERROR, E.RUN_END]
    assert device.events[5][1]["code"] == "openai-unavailable"
    assert device.times_of(E.INTENT_END)[0] - device.times_of(E.STT_VAD_END)[0] >= 0.05
    assert streams.created[0].finished


async def test_response_timeout(make_conv, device, session, streams):
    conv = make_conv(response_timeout_s=0.2)
    await begin(conv, session)
    session.push(*SPEECH)
    await wait_for(lambda: E.RUN_END in device.types())
    assert device.types()[4:] == [E.INTENT_END, E.ERROR, E.RUN_END]
    assert device.events[5][1]["code"] == "timeout"
    assert device.times_of(E.ERROR)[0] - device.times_of(E.STT_VAD_END)[0] >= 0.19
    assert session.closed
    assert streams.created[0].finished


async def push_deltas(session, rid, seconds, every=0.1):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        session.push(AudioDelta(rid, b"\x00\x00" * 240))
        await asyncio.sleep(every)


async def test_long_answer_not_cut_while_audio_flows(make_conv, device, session):
    conv = make_conv(response_timeout_s=0.2)
    await begin(conv, session)
    session.push(*SPEECH, ResponseCreated("r1"))
    await push_deltas(session, "r1", 0.6)
    session.push(TranscriptDone("r1", "Довга відповідь."), ResponseDone("r1", "completed", (), None, {}))
    await wait_for(lambda: E.RUN_END in device.types())
    assert device.types() == HAPPY
    assert E.ERROR not in device.types()


async def test_silence_while_answering_times_out(make_conv, device, session, streams):
    conv = make_conv(response_timeout_s=0.6)
    await begin(conv, session)
    session.push(*SPEECH, ResponseCreated("r1"))
    await wait_for(lambda: E.STT_VAD_END in device.types())
    await asyncio.sleep(0.3)
    delta_at = time.monotonic()
    session.push(AudioDelta("r1", b"\x00\x00" * 240))
    await wait_for(lambda: E.RUN_END in device.types(), timeout=2.0)
    assert device.types()[-3:] == [E.INTENT_END, E.ERROR, E.RUN_END]
    assert device.events[-2][1]["code"] == "timeout"
    assert device.times_of(E.ERROR)[0] - delta_at >= 0.55
    assert streams.created[0].finished


async def test_answer_hard_cap(make_conv, device, session):
    conv = make_conv(answer_max_s=0.3)
    await begin(conv, session)
    session.push(*SPEECH, ResponseCreated("r1"))
    await push_deltas(session, "r1", 0.6)
    await wait_for(lambda: E.RUN_END in device.types())
    assert "cancel" in session.calls
    assert device.types()[-3:] == [E.INTENT_END, E.ERROR, E.RUN_END]
    assert device.events[-2][1]["code"] == "timeout"
    assert 0.29 <= device.times_of(E.ERROR)[0] - device.times_of(E.STT_VAD_END)[0] < 0.5
    assert session.closed


async def test_tool_round_trip_rearms_answer_timer(make_conv, device, session):
    conv = make_conv(response_timeout_s=0.5)
    session.tools_delay = 0.4
    await begin(conv, session)
    call = ToolCall("c1", "set_volume", '{"action":"down"}')
    session.push(*SPEECH, ResponseCreated("r1"))
    await asyncio.sleep(0.3)
    session.push(ResponseDone("r1", "completed", (call,), None, {}))
    await wait_for(lambda: any(isinstance(c, tuple) for c in session.calls), timeout=2.0)
    await asyncio.sleep(0.3)
    session.push(*answer("r2", "Зробив тихіше."))
    await wait_for(lambda: E.RUN_END in device.types(), timeout=2.0)
    assert device.types() == HAPPY


async def test_send_failure_mid_turn_reports_error(conv, device, session, streams):
    session.fail_on = {"append"}
    assert await conv.handle_start("", 0, SETTINGS, "Hey Jarvis") == 0
    for _ in range(2):
        await conv.handle_audio(CHUNK)
    await wait_for(lambda: E.RUN_END in device.types())
    assert device.types() == [E.RUN_START, E.STT_START, E.INTENT_END, E.ERROR, E.RUN_END]
    assert device.events[3][1]["code"] == "openai-unavailable"
    assert session.close_calls == 1
    assert streams.created[0].finished


async def test_stop_cancels_response_and_ignores_stale_events(conv, device, session, streams):
    await begin(conv, session)
    session.push(*SPEECH, ResponseCreated("r1"), AudioDelta("r1", b"\x00\x00" * 2400))
    await wait_for(lambda: E.INTENT_PROGRESS in device.types())
    await conv.handle_stop(True)
    assert "cancel" in session.calls
    assert device.types()[-1] == E.RUN_END and device.types().count(E.RUN_END) == 1
    assert streams.created[0].finished
    assert not session.closed

    await begin(conv, session)
    session.push(AudioDelta("r1", b"\x00\x00" * 2400), ResponseDone("r1", "cancelled", (), None, {}))
    session.push(*SPEECH, *answer("r2"))
    await wait_for(lambda: device.types().count(E.RUN_END) == 2)
    assert device.types()[6:] == HAPPY
    assert streams.created[1].finished


async def test_stop_before_announce_still_pairs_run_start(conv, device, streams):
    assert await conv.handle_start("", 0, SETTINGS, "Hey Jarvis") == 0
    await conv.handle_stop(True)
    await asyncio.sleep(0.05)
    assert device.types() == [E.RUN_START, E.STT_START, E.RUN_END]
    assert device.events[0][1] == {"url": audio_url(streams.created[0].id)}
    assert streams.created[0].finished


async def test_stop_while_speaking_cancels_response(conv, device, session):
    await begin(conv, session)
    session.push(SpeechStarted())
    await wait_for(lambda: E.STT_VAD_START in device.types())
    await conv.handle_stop(True)
    assert session.calls == ["clear", "cancel", "clear"]
    assert device.types() == [E.RUN_START, E.STT_START, E.STT_VAD_START, E.RUN_END]

    await begin(conv, session)
    session.push(ResponseCreated("r1"), AudioDelta("r1", b"\x00\x00" * 2400), ResponseDone("r1", "cancelled", (), None, {}))
    session.push(*SPEECH, *answer("r2"))
    await wait_for(lambda: device.types().count(E.RUN_END) == 2)
    assert device.types()[4:] == HAPPY


async def test_new_start_while_speaking_cancels_response(conv, device, session, streams):
    await begin(conv, session)
    session.push(SpeechStarted())
    await wait_for(lambda: E.STT_VAD_START in device.types())
    await begin(conv, session)
    assert session.calls == ["clear", "cancel", "clear"]
    assert streams.created[0].finished
    session.push(ResponseCreated("r1"), ResponseDone("r1", "cancelled", (), None, {}), *SPEECH, *answer("r2"))
    await wait_for(lambda: E.RUN_END in device.types())
    assert device.types()[3:] == HAPPY


async def test_cancel_failure_after_stop_drops_session(conv, device, session, factory):
    session.fail_on = {"cancel"}
    await begin(conv, session)
    session.push(*SPEECH, ResponseCreated("r1"), AudioDelta("r1", b"\x00\x00" * 2400))
    await wait_for(lambda: E.INTENT_PROGRESS in device.types())
    await conv.handle_stop(False)
    assert device.types()[-1] == E.RUN_END and E.ERROR not in device.types()
    assert session.close_calls == 1
    second = FakeSession()
    factory.sessions.append(second)
    await begin(conv, second)
    assert factory.calls == 2


async def test_stop_without_turn_sends_run_end(conv, device, session, factory):
    await conv.handle_stop(True)
    await conv.handle_stop(False)
    assert device.events == [(E.RUN_END, {}), (E.RUN_END, {})] and factory.calls == 0
    await begin(conv, session)
    session.push(*SPEECH, *answer("r1"))
    await wait_for(lambda: device.types().count(E.RUN_END) == 3)
    await conv.handle_stop(True)
    assert device.types() == [E.RUN_END, E.RUN_END, *HAPPY, E.RUN_END]
    assert session.calls.count("cancel") == 0


async def test_stop_during_answer_grace_ends_conversation(make_conv, device, session):
    conv = make_conv(mic_stop_grace_s=0.5)
    await begin(conv, session)
    session.push(*SPEECH, ResponseCreated("r1"), TranscriptDone("r1", "Так."), ResponseDone("r1", "completed", (), None, {}))
    await wait_for(lambda: E.STT_VAD_END in device.types())
    await asyncio.sleep(0.15)
    await conv.handle_stop(True)
    await wait_for(lambda: E.RUN_END in device.types())
    intent = [data for event, data in device.events if event == E.INTENT_END]
    assert intent[0]["continue_conversation"] == "0" and intent[0]["conversation_id"]
    assert device.types().count(E.RUN_END) == 1
    started = time.monotonic()
    await begin(conv, session)
    await wait_for(lambda: device.types().count(E.RUN_END) == 2, timeout=2.0)
    assert 0.9 <= device.times[-1] - started <= 1.5


async def test_stop_while_turn_is_ending_adds_no_run_end(make_conv, device, session):
    conv = make_conv(mic_stop_grace_s=0.3)
    await begin(conv, session)
    session.push(*SPEECH, ResponseCreated("r1"), TranscriptDone("r1", "Так."), ResponseDone("r1", "completed", (), None, {}))
    await wait_for(lambda: E.STT_VAD_END in device.types())
    await asyncio.sleep(0.1)
    await conv.handle_stop(True)
    await wait_for(lambda: E.RUN_END in device.types())
    await asyncio.sleep(0.05)
    assert device.types().count(E.RUN_END) == 1


async def test_stop_after_answer_clears_follow_up(conv, device, session):
    await begin(conv, session)
    session.push(*SPEECH, *answer("r1"))
    await wait_for(lambda: E.RUN_END in device.types())
    count = len(device.events)
    await conv.handle_stop(True)
    assert device.events[count:] == [(E.RUN_END, {})]
    started = time.monotonic()
    await begin(conv, session)
    await wait_for(lambda: device.types().count(E.RUN_END) == 3, timeout=2.0)
    assert 0.9 <= device.times[-1] - started <= 1.5
    assert device.types()[count + 1 :] == [E.RUN_START, E.STT_START, E.RUN_END]


async def test_session_dropped_after_the_answer_makes_the_next_start_a_first_turn(conv, device, session, factory,
                                                                                  caplog):
    caplog.set_level("INFO", logger="voice_hub.conversation")
    await begin(conv, session)
    session.push(*SPEECH, *answer("r1"))
    await wait_for(lambda: E.RUN_END in device.types())
    assert device.events[5][1]["continue_conversation"] == "1"
    session.push(Closed())
    await wait_for(lambda: session.close_calls == 1)
    second = FakeSession()
    factory.sessions.append(second)
    started = time.monotonic()
    await begin(conv, second)
    await wait_for(lambda: device.types().count(E.RUN_END) == 2, timeout=2.0)
    assert 0.9 <= device.times[-1] - started <= 1.5
    starts = [r.getMessage() for r in caplog.records if r.getMessage().startswith("turn started on")]
    assert starts == ["turn started on kitchen (first turn)"] * 2


async def test_new_start_replaces_running_turn(conv, device, session, streams):
    await begin(conv, session)
    session.push(*SPEECH, ResponseCreated("r1"), AudioDelta("r1", b"\x00\x00" * 2400))
    await wait_for(lambda: E.INTENT_PROGRESS in device.types())
    await begin(conv, session)
    old, new = streams.created
    assert old.finished and not new.finished
    assert session.calls == ["clear", "cancel", "clear"]
    assert device.types() == [
        E.RUN_START,
        E.STT_START,
        E.STT_VAD_START,
        E.STT_VAD_END,
        E.INTENT_PROGRESS,
        E.RUN_START,
        E.STT_START,
    ]
    assert device.events[5][1]["url"] == audio_url(new.id) != device.events[0][1]["url"]
    session.push(ResponseDone("r1", "cancelled", (), None, {}), *SPEECH, *answer("r2"))
    await wait_for(lambda: E.RUN_END in device.types())
    assert device.types()[5:] == HAPPY
    assert new.finished


async def test_session_closed_after_idle(make_conv, device, session, factory):
    conv = make_conv(session_idle_s=0.2)
    await begin(conv, session)
    session.push(*SPEECH, *answer("r1"))
    await wait_for(lambda: E.RUN_END in device.types())
    assert not session.closed
    await wait_for(lambda: session.closed)
    second = FakeSession()
    factory.sessions.append(second)
    await begin(conv, second)
    assert factory.calls == 2
    second.push(*SPEECH, *answer("r2"))
    await wait_for(lambda: device.types().count(E.RUN_END) == 2)
    first_cid, second_cid = [data["conversation_id"] for event, data in device.events if event == E.INTENT_END]
    assert first_cid != second_cid


async def test_idle_close_waits_for_playback(make_conv, device, session):
    conv = make_conv(session_idle_s=0.2)
    await begin(conv, session)
    pcm = b"\x00\x00" * 12000
    session.push(*SPEECH, ResponseCreated("r1"), AudioDelta("r1", pcm), TranscriptDone("r1", "Так."))
    session.push(ResponseDone("r1", "completed", (), None, {}))
    await wait_for(lambda: E.RUN_END in device.types())
    ended = device.times_of(E.RUN_END)[0]
    await asyncio.sleep(ended + 0.4 - time.monotonic())
    assert not session.closed
    await wait_for(lambda: session.closed, timeout=1.0)
    assert 0.65 <= time.monotonic() - ended <= 0.9


async def test_session_reused_within_idle(conv, device, session, factory):
    for rid in ("r1", "r2"):
        await begin(conv, session)
        session.push(*SPEECH, *answer(rid))
        await wait_for(lambda: device.types().count(E.RUN_END) == int(rid[1]))
    assert factory.calls == 1 and not session.closed
    first_cid, second_cid = [data["conversation_id"] for event, data in device.events if event == E.INTENT_END]
    assert first_cid == second_cid


async def test_dead_reused_session_is_replaced(make_conv, device, session, factory):
    conv = make_conv(ready_check_s=0.2)
    await begin(conv, session)
    session.push(*SPEECH, *answer("r1"))
    await wait_for(lambda: E.RUN_END in device.types())
    session.silent = True
    sent = len(session.appended)
    second = FakeSession()
    factory.sessions.append(second)
    assert await conv.handle_start("", 0, SETTINGS, None) == 0
    for _ in range(3):
        await conv.handle_audio(CHUNK)
    await wait_for(lambda: "clear" in second.calls, timeout=1.0)
    await wait_for(lambda: session.closed)
    assert factory.calls == 2
    assert session.calls.count("clear") == 2 and len(session.appended) == sent
    await wait_for(lambda: len(second.appended) > 0)
    second.push(*SPEECH, *answer("r2"))
    await wait_for(lambda: device.types().count(E.RUN_END) == 2)
    assert device.types()[8:] == HAPPY
    first_cid, second_cid = [data["conversation_id"] for event, data in device.events if event == E.INTENT_END]
    assert first_cid != second_cid


async def test_dead_reused_session_and_failed_reconnect_report_error(make_conv, device, session, factory):
    conv = make_conv(ready_check_s=0.2)
    await begin(conv, session)
    session.push(*SPEECH, *answer("r1"))
    await wait_for(lambda: E.RUN_END in device.types())
    session.silent = True
    factory.sessions.append(FakeSession(fail_connect=True))
    assert await conv.handle_start("", 0, SETTINGS, None) == 0
    await wait_for(lambda: device.types().count(E.RUN_END) == 2)
    assert device.types()[8:] == [E.RUN_START, E.STT_START, E.INTENT_END, E.ERROR, E.RUN_END]
    assert device.events[-2][1]["code"] == "openai-unavailable"
    await wait_for(lambda: session.closed)
    assert factory.calls == 2


async def test_fresh_session_is_not_checked(make_conv, device, session, factory):
    conv = make_conv(ready_check_s=5.0)
    session.silent = True
    await begin(conv, session)
    for _ in range(3):
        await conv.handle_audio(CHUNK)
    await wait_for(lambda: len(session.appended) > 0)
    session.push(*SPEECH, *answer("r1"))
    await wait_for(lambda: E.RUN_END in device.types())
    assert device.types() == HAPPY
    assert factory.calls == 1


async def test_dead_session_close_does_not_delay_replacement(make_conv, device, session, factory):
    conv = make_conv(ready_check_s=0.2)
    await begin(conv, session)
    session.push(*SPEECH, *answer("r1"))
    await wait_for(lambda: E.RUN_END in device.types())
    session.silent = True
    session.close_delay = 1.0
    second = FakeSession()
    factory.sessions.append(second)
    started = time.monotonic()
    assert await conv.handle_start("", 0, SETTINGS, None) == 0
    for _ in range(3):
        await conv.handle_audio(CHUNK)
    await wait_for(lambda: len(second.appended) > 0, timeout=2.0)
    assert time.monotonic() - started < 0.6
    await wait_for(lambda: session.close_calls == 1)


async def test_closed_during_ready_check_reconnects(make_conv, device, session, factory):
    conv = make_conv(ready_check_s=1.0)
    await begin(conv, session)
    session.push(*SPEECH, *answer("r1"))
    await wait_for(lambda: E.RUN_END in device.types())
    session.silent = True
    second = FakeSession()
    factory.sessions.append(second)
    started = time.monotonic()
    assert await conv.handle_start("", 0, SETTINGS, None) == 0
    await wait_for(lambda: session.calls.count("clear") == 2)
    session.push(Closed())
    for _ in range(3):
        await conv.handle_audio(CHUNK)
    await wait_for(lambda: len(second.appended) > 0, timeout=2.0)
    assert time.monotonic() - started < 0.6
    assert factory.calls == 2 and session.close_calls == 1
    assert E.ERROR not in device.types()
    second.push(*SPEECH, *answer("r2"))
    await wait_for(lambda: device.types().count(E.RUN_END) == 2)
    assert device.types()[8:] == HAPPY


async def test_closed_on_fresh_session_before_ready_reports_error(conv, device, session, factory):
    session.clear_delay = 0.1
    session.push(Closed())
    assert await conv.handle_start("", 0, SETTINGS, "Hey Jarvis") == 0
    await wait_for(lambda: E.RUN_END in device.types())
    assert device.types() == [E.RUN_START, E.STT_START, E.INTENT_END, E.ERROR, E.RUN_END]
    assert device.events[3][1]["code"] == "openai-unavailable"
    assert factory.calls == 1 and session.close_calls == 1


async def test_debug_input_replaces_mic_first_turn_only(make_conv, device, session):
    debug = b"\x01\x00" * 1600
    conv = make_conv(debug_input=debug)
    feed = debug + bytes(-len(debug) % 1024) + bytes(1024 * 40)
    resampler = Resampler(16000, 24000)
    outputs = [resampler.process(feed[i : i + 1024]) for i in range(0, len(feed), 1024)]
    cumulative = np.cumsum([0] + [len(o) for o in outputs])
    expected = b"".join(outputs)
    loud = b"\x10\x27" * 512

    started = time.monotonic()
    await begin(conv, session)
    for _ in range(4):
        await conv.handle_audio(loud)
    await wait_for(lambda: len(session.appended) >= cumulative[6])
    elapsed = time.monotonic() - started
    sent = bytes(session.appended)
    assert len(sent) <= cumulative[int(elapsed / 0.032) + 2]
    assert pcm_matches(sent, expected[: len(sent)])

    session.push(*SPEECH, *answer("r1"))
    await wait_for(lambda: E.RUN_END in device.types())
    first_turn = len(session.appended)
    assert pcm_matches(bytes(session.appended), expected[:first_turn])

    await begin(conv, session)
    for _ in range(4):
        await conv.handle_audio(loud)
    await wait_for(lambda: device.types().count(E.RUN_END) == 2)
    follow_up = bytes(session.appended[first_turn:])
    silence = Resampler(16000, 24000)
    expected_silence = b"".join(silence.process(bytes(1024)) for _ in range(40))
    assert len(follow_up) > 0 and pcm_matches(follow_up, expected_silence[: len(follow_up)])
    assert device.types()[8:] == [E.RUN_START, E.STT_START, E.RUN_END]


async def test_close_finishes_stream_and_closes_session(conv, device, session, streams, factory):
    await begin(conv, session)
    session.push(*SPEECH, ResponseCreated("r1"), AudioDelta("r1", b"\x00\x00" * 2400))
    await wait_for(lambda: E.INTENT_PROGRESS in device.types())
    await conv.close()
    assert streams.created[0].finished
    assert session.closed and session.close_calls == 1
    count = len(device.events)
    await asyncio.sleep(0.1)
    assert len(device.events) == count
    second = FakeSession()
    factory.sessions.append(second)
    await begin(conv, second)
    assert factory.calls == 2


async def test_close_during_error_grace_closes_dropped_session(make_conv, device, session):
    conv = make_conv(mic_stop_grace_s=0.5)
    await begin(conv, session)
    session.push(*SPEECH, ResponseCreated("r1"), ResponseDone("r1", "failed", (), "server_error", {}))
    await wait_for(lambda: E.STT_VAD_END in device.types())
    await asyncio.sleep(0.1)
    await conv.close()
    assert E.ERROR not in device.types()
    assert session.closed and session.close_calls == 1


async def test_close_while_connecting(conv, device, session, streams):
    session.connect_delay = 5.0
    assert await conv.handle_start("", 0, SETTINGS, "Hey Jarvis") == 0
    await wait_for(lambda: "connect" in session.trace)
    await asyncio.wait_for(conv.close(), 0.5)
    assert streams.created[0].finished
    assert device.types() == [E.RUN_START, E.STT_START]


async def test_a_start_that_fails_after_making_a_stream_leaves_no_stream_behind(device, streams, factory):
    def no_url(stream_id: str) -> str:
        raise RuntimeError("no address for the audio")

    conv = Conversation(device, streams, factory, TIMING, no_url)
    try:
        assert await conv.handle_start("", 0, SETTINGS, "Hey Jarvis") is None
        (stream,) = streams.created
        assert stream.finished
        request = make_mocked_request("GET", f"/audio/{stream.id}.wav", match_info={"stream_id": stream.id})
        with pytest.raises(web.HTTPNotFound):
            await asyncio.wait_for(streams.handle(request), 1.0)
        assert device.events == [] and factory.calls == 0
    finally:
        await conv.close()


async def test_handle_start_returns_none_on_internal_error(conv, device, streams, factory):
    streams.fail = True
    assert await conv.handle_start("", 0, SETTINGS, "Hey Jarvis") is None
    await asyncio.sleep(0.05)
    assert device.events == [] and factory.calls == 0
