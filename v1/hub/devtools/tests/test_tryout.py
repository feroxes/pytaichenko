import asyncio
import base64
import json
import time
import wave
from pathlib import Path

import pytest

from devtools import tryout
from devtools.tryout import TryoutResult, estimate_cost, main, parse_args, run_tryout
from voice_hub.audio import Resampler, read_wav_pcm
from voice_hub.config import OpenAIConfig
from voice_hub.realtime import RealtimeError

SAMPLES = Path(__file__).parent.parent.parent / "samples"
USAGE = {
    "input_token_details": {
        "audio_tokens": 100,
        "text_tokens": 50,
        "cached_tokens_details": {"audio_tokens": 40, "text_tokens": 10},
    },
    "output_token_details": {"audio_tokens": 30, "text_tokens": 5},
}


@pytest.fixture(autouse=True)
def short_grace(monkeypatch):
    monkeypatch.setattr(tryout, "QUIET_GRACE_S", 0.05)


def cfg(url, **kw):
    return OpenAIConfig(api_key="sk-test", url=url, **kw)


def delta(response_id, nbytes, fill=0):
    return {
        "type": "response.output_audio.delta",
        "response_id": response_id,
        "delta": base64.b64encode(bytes([fill]) * nbytes).decode("ascii"),
    }


def done(response_id, status="completed", output=(), usage=None):
    response = {"id": response_id, "status": status, "output": list(output)}
    if usage is not None:
        response["usage"] = usage
    return {"type": "response.done", "response": response}


def script(fake, replies):
    state = {"armed": False, "asked": 0, "tool_replies": 0}

    async def on_message(msg):
        kind = msg["type"]
        if kind == "input_audio_buffer.clear":
            state["armed"] = True
        elif kind == "input_audio_buffer.append" and state["armed"]:
            state["armed"] = False
            await replies.on_question(state["asked"])
            state["asked"] += 1
        elif kind == "response.create":
            await replies.on_tool_output(state["tool_replies"])
            state["tool_replies"] += 1

    fake.on_message = on_message


class Plain:
    def __init__(self, fake, answers, speech_s=0.0, gap_s=0.0):
        self.fake = fake
        self.answers = answers
        self.speech_s = speech_s
        self.gap_s = gap_s

    async def on_question(self, index):
        transcript, nbytes, usage = self.answers[index]
        rid = f"r{index + 1}"
        await self.fake.push({"type": "input_audio_buffer.speech_started"})
        await asyncio.sleep(self.speech_s)
        await self.fake.push({"type": "input_audio_buffer.speech_stopped"})
        await self.fake.push({"type": "response.created", "response": {"id": rid}})
        await asyncio.sleep(self.gap_s)
        for msg in (
            delta(rid, nbytes),
            {"type": "response.output_audio_transcript.done", "response_id": rid, "transcript": transcript},
            done(rid, usage=usage),
        ):
            await self.fake.push(msg)

    async def on_tool_output(self, index):
        raise AssertionError("unexpected response.create")


class WithTool:
    def __init__(self, fake):
        self.fake = fake

    async def on_question(self, index):
        call = {"type": "function_call", "call_id": "call_1", "name": "set_volume", "arguments": '{"action":"down"}'}
        for msg in (
            {"type": "input_audio_buffer.speech_started"},
            {"type": "input_audio_buffer.speech_stopped"},
            {"type": "response.created", "response": {"id": "r1"}},
            done("r1", output=[call], usage=USAGE),
        ):
            await self.fake.push(msg)

    async def on_tool_output(self, index):
        for msg in (
            {"type": "response.created", "response": {"id": "r2"}},
            delta("r2", 4800),
            {"type": "response.output_audio_transcript.done", "response_id": "r2", "transcript": "Гучність 55"},
            done("r2", usage=USAGE),
        ):
            await self.fake.push(msg)


VOLUME_CALL = {"type": "function_call", "call_id": "v1", "name": "set_volume", "arguments": '{"action":"down"}'}
END_CALL = {"type": "function_call", "call_id": "e1", "name": "end_conversation", "arguments": "{}"}


class Ending:
    def __init__(self, fake, calls, split=False):
        self.fake = fake
        self.calls = calls
        self.split = split

    async def on_question(self, index):
        if self.split:
            for msg in (
                {"type": "input_audio_buffer.speech_started"},
                {"type": "input_audio_buffer.speech_stopped"},
                {"type": "response.created", "response": {"id": "r0"}},
                delta("r0", 4800, 1),
                {"type": "response.output_audio_transcript.done", "response_id": "r0", "transcript": "a"},
                done("r0", usage=USAGE),
            ):
                await self.fake.push(msg)
            await asyncio.sleep(0.1)
        for msg in (
            {"type": "input_audio_buffer.speech_started"},
            {"type": "input_audio_buffer.speech_stopped"},
            {"type": "response.created", "response": {"id": "r1"}},
            delta("r1", 4800, 2),
            {"type": "response.output_audio_transcript.done", "response_id": "r1", "transcript": "До зустрічі!"},
            done("r1", output=self.calls, usage=USAGE),
        ):
            await self.fake.push(msg)

    async def on_tool_output(self, index):
        raise AssertionError("unexpected response.create")


class Split:
    def __init__(self, fake, pause_s=0.1):
        self.fake = fake
        self.pause_s = pause_s

    async def turn(self, rid, transcript, nbytes, fill, speech_started=True):
        if speech_started:
            await self.fake.push({"type": "input_audio_buffer.speech_started"})
        for msg in (
            {"type": "input_audio_buffer.speech_stopped"},
            {"type": "response.created", "response": {"id": rid}},
            delta(rid, nbytes, fill),
            {"type": "response.output_audio_transcript.done", "response_id": rid, "transcript": transcript},
            done(rid, usage=USAGE),
        ):
            await self.fake.push(msg)

    async def on_question(self, index):
        if index == 0:
            await self.turn("r1", "a", 4800, 1)
            await asyncio.sleep(self.pause_s)
            await self.turn("r2", "b", 9600, 2)
        else:
            await self.turn("r3", "c", 2400, 3)

    async def on_tool_output(self, index):
        raise AssertionError("unexpected response.create")


class Slow:
    def __init__(self, fake, delay_s=0.15):
        self.fake = fake
        self.delay_s = delay_s

    async def on_question(self, index):
        call = {"type": "function_call", "call_id": "call_1", "name": "set_volume", "arguments": '{"action":"down"}'}
        for msg in (
            {"type": "input_audio_buffer.speech_started"},
            {"type": "input_audio_buffer.speech_stopped"},
            {"type": "response.created", "response": {"id": "r1"}},
            delta("r1", 4800, 1),
            {"type": "response.output_audio_transcript.done", "response_id": "r1", "transcript": "a"},
            done("r1", usage=USAGE),
            {"type": "input_audio_buffer.speech_started"},
        ):
            await self.fake.push(msg)
        await asyncio.sleep(self.delay_s)
        for msg in (
            {"type": "input_audio_buffer.speech_stopped"},
            {"type": "response.created", "response": {"id": "r2"}},
        ):
            await self.fake.push(msg)
        await asyncio.sleep(self.delay_s)
        await self.fake.push(done("r2", output=[call], usage=USAGE))

    async def on_tool_output(self, index):
        await asyncio.sleep(self.delay_s)
        for msg in (
            {"type": "response.created", "response": {"id": "r3"}},
            delta("r3", 4800, 3),
            {"type": "response.output_audio_transcript.done", "response_id": "r3", "transcript": "c"},
            done("r3", usage=USAGE),
        ):
            await self.fake.push(msg)


def kinds(fake):
    return [m["type"] for m in fake.received]


def test_estimate_cost():
    u = {
        "input_token_details": {
            "audio_tokens": 1000,
            "text_tokens": 1000,
            "cached_tokens_details": {"audio_tokens": 400, "text_tokens": 0},
        },
        "output_token_details": {"audio_tokens": 1000, "text_tokens": 1000},
    }
    assert estimate_cost(u) == pytest.approx(600 * 10e-6 + 400 * 0.3e-6 + 1000 * 0.6e-6 + 1000 * 20e-6 + 1000 * 2.4e-6)


def test_estimate_cost_prices_cached_text():
    u = {"input_token_details": {"text_tokens": 1000, "cached_tokens_details": {"text_tokens": 1000}}}
    assert estimate_cost(u) == pytest.approx(1000 * 0.06e-6)


@pytest.mark.parametrize(
    "usage",
    [
        {},
        {"total_tokens": 5},
        {"input_token_details": None, "output_token_details": None},
        {"input_token_details": {"audio_tokens": None, "cached_tokens_details": None}},
    ],
)
def test_estimate_cost_treats_missing_as_zero(usage):
    assert estimate_cost(usage) == 0.0


def test_estimate_cost_with_partial_details():
    assert estimate_cost({"output_token_details": {"audio_tokens": 500}}) == pytest.approx(500 * 20e-6)


async def test_run_tryout_writes_answer(fake_openai, tmp_path):
    script(fake_openai, Plain(fake_openai, [("56", 4800, USAGE)]))
    out = tmp_path / "nested" / "out"
    [r] = await run_tryout(cfg(fake_openai.url), [SAMPLES / "q1.wav"], out)
    assert isinstance(r, TryoutResult)
    assert r.answer_path == out / "answer-1.wav"
    with wave.open(str(r.answer_path)) as w:
        assert w.getframerate() == 24000 and w.getnframes() == 2400
        assert w.getnchannels() == 1 and w.getsampwidth() == 2
    assert r.transcript == "56"
    assert r.first_audio_s is not None and r.first_audio_s >= 0
    assert r.usage == USAGE
    assert r.turns == 1
    assert r.ended is False


async def test_first_audio_is_measured_from_end_of_speech(fake_openai, tmp_path):
    script(fake_openai, Plain(fake_openai, [("56", 4800, USAGE)], speech_s=0.4, gap_s=0.1))
    [r] = await run_tryout(cfg(fake_openai.url), [SAMPLES / "q1.wav"], tmp_path)
    assert 0.09 <= r.first_audio_s < 0.3


async def test_run_tryout_streams_resampled_audio_then_silence(fake_openai, tmp_path):
    script(fake_openai, Plain(fake_openai, [("56", 4800, USAGE)]))
    await run_tryout(cfg(fake_openai.url), [SAMPLES / "q1.wav"], tmp_path)
    assert kinds(fake_openai)[:3] == ["session.update", "input_audio_buffer.clear", "input_audio_buffer.append"]
    assert set(kinds(fake_openai)[2:]) == {"input_audio_buffer.append"}
    chunks = [base64.b64decode(m["audio"]) for m in fake_openai.received[2:]]
    assert all(0 < len(c) <= 4800 for c in chunks)
    assert all(len(c) == 4800 for c in chunks[:-1])
    rate, pcm = read_wav_pcm(SAMPLES / "q1.wav")
    resampler = Resampler(rate, 24000)
    expected = resampler.process(pcm) + resampler.flush() + bytes(2 * 24000 * 2)
    assert b"".join(chunks) == expected


async def test_run_tryout_keeps_one_session_for_all_questions(fake_openai, tmp_path):
    script(fake_openai, Plain(fake_openai, [("56", 4800, USAGE), ("63", 9600, None)]))
    results = await run_tryout(cfg(fake_openai.url), [SAMPLES / "q1.wav", SAMPLES / "q4.wav"], tmp_path)
    assert [r.answer_path.name for r in results] == ["answer-1.wav", "answer-2.wav"]
    assert [r.transcript for r in results] == ["56", "63"]
    assert [r.turns for r in results] == [1, 1]
    assert results[1].usage == {}
    with wave.open(str(results[1].answer_path)) as w:
        assert w.getnframes() == 4800
    assert kinds(fake_openai).count("session.update") == 1
    clears = [i for i, k in enumerate(kinds(fake_openai)) if k == "input_audio_buffer.clear"]
    assert len(clears) == 2 and clears[0] == 1
    assert set(kinds(fake_openai)[clears[1] + 1 :]) == {"input_audio_buffer.append"}


async def test_run_tryout_answers_tool_call(fake_openai, tmp_path):
    script(fake_openai, WithTool(fake_openai))
    [r] = await run_tryout(cfg(fake_openai.url), [SAMPLES / "q3.wav"], tmp_path)
    sent = [m for m in fake_openai.received if m["type"] != "input_audio_buffer.append"]
    assert [m["type"] for m in sent] == [
        "session.update",
        "input_audio_buffer.clear",
        "conversation.item.create",
        "response.create",
    ]
    assert sent[2]["item"] == {"type": "function_call_output", "call_id": "call_1", "output": '{"volume": 55}'}
    with wave.open(str(r.answer_path)) as w:
        assert w.getnframes() == 2400
    assert r.transcript == "Гучність 55"
    assert r.first_audio_s is not None
    assert r.turns == 1
    assert r.usage == {
        "input_token_details": {
            "audio_tokens": 200,
            "text_tokens": 100,
            "cached_tokens_details": {"audio_tokens": 80, "text_tokens": 20},
        },
        "output_token_details": {"audio_tokens": 60, "text_tokens": 10},
    }
    assert estimate_cost(r.usage) == pytest.approx(2 * estimate_cost(USAGE))


async def test_run_tryout_ends_when_the_model_ends_the_conversation(fake_openai, tmp_path, monkeypatch):
    monkeypatch.setattr(tryout, "QUIET_GRACE_S", 5.0)
    script(fake_openai, Ending(fake_openai, [END_CALL]))
    started = time.monotonic()
    [r] = await run_tryout(cfg(fake_openai.url), [SAMPLES / "q1.wav"], tmp_path)
    assert time.monotonic() - started < 2.0
    sent = [m for m in fake_openai.received if m["type"] != "input_audio_buffer.append"]
    assert [m["type"] for m in sent] == ["session.update", "input_audio_buffer.clear", "conversation.item.create"]
    assert sent[2]["item"] == {"type": "function_call_output", "call_id": "e1", "output": '{"ok": true}'}
    assert r.ended is True
    assert r.turns == 1
    assert r.transcript == "До зустрічі!"
    assert r.usage == USAGE
    assert r.first_audio_s is not None
    with wave.open(str(r.answer_path)) as w:
        assert w.readframes(w.getnframes()) == b"\x02" * 4800


async def test_run_tryout_answers_every_call_of_the_ending_response(fake_openai, tmp_path):
    script(fake_openai, Ending(fake_openai, [VOLUME_CALL, END_CALL]))
    [r] = await run_tryout(cfg(fake_openai.url), [SAMPLES / "q1.wav"], tmp_path)
    sent = [m for m in fake_openai.received if m["type"] != "input_audio_buffer.append"]
    assert [m["type"] for m in sent] == [
        "session.update",
        "input_audio_buffer.clear",
        "conversation.item.create",
        "conversation.item.create",
    ]
    assert [(m["item"]["call_id"], json.loads(m["item"]["output"])) for m in sent[2:]] == [
        ("v1", {"volume": 55}),
        ("e1", {"ok": True}),
    ]
    assert "response.create" not in kinds(fake_openai)
    assert r.ended is True


async def test_run_tryout_ending_is_a_final_answer_after_a_vad_split(fake_openai, tmp_path, monkeypatch):
    monkeypatch.setattr(tryout, "QUIET_GRACE_S", 5.0)
    script(fake_openai, Ending(fake_openai, [END_CALL], split=True))
    started = time.monotonic()
    [r] = await run_tryout(cfg(fake_openai.url), [SAMPLES / "q1.wav"], tmp_path)
    assert time.monotonic() - started < 2.0
    assert r.ended is True
    assert r.turns == 2
    assert r.transcript == "a До зустрічі!"
    assert "response.create" not in kinds(fake_openai)
    with wave.open(str(r.answer_path)) as w:
        assert w.readframes(w.getnframes()) == b"\x01" * 4800 + b"\x02" * 4800


async def test_run_tryout_joins_turns_split_by_vad(fake_openai, tmp_path, monkeypatch):
    monkeypatch.setattr(tryout, "QUIET_GRACE_S", 0.4)
    script(fake_openai, Split(fake_openai, pause_s=0.1))
    first, second = await run_tryout(cfg(fake_openai.url), [SAMPLES / "q1.wav", SAMPLES / "q4.wav"], tmp_path)
    assert first.turns == 2
    assert first.transcript == "a b"
    assert first.usage == {
        "input_token_details": {
            "audio_tokens": 200,
            "text_tokens": 100,
            "cached_tokens_details": {"audio_tokens": 80, "text_tokens": 20},
        },
        "output_token_details": {"audio_tokens": 60, "text_tokens": 10},
    }
    assert first.first_audio_s is not None
    with wave.open(str(first.answer_path)) as w:
        assert w.readframes(w.getnframes()) == b"\x01" * 4800 + b"\x02" * 9600
    assert second.turns == 1
    assert second.transcript == "c"
    assert second.usage == USAGE
    with wave.open(str(second.answer_path)) as w:
        assert w.readframes(w.getnframes()) == b"\x03" * 2400


async def test_run_tryout_waits_for_speech_responses_and_tool_round_trips(fake_openai, tmp_path):
    script(fake_openai, Slow(fake_openai))
    [r] = await run_tryout(cfg(fake_openai.url), [SAMPLES / "q1.wav"], tmp_path)
    assert r.turns == 2
    assert r.transcript == "a c"
    assert estimate_cost(r.usage) == pytest.approx(3 * estimate_cost(USAGE))
    with wave.open(str(r.answer_path)) as w:
        assert w.readframes(w.getnframes()) == b"\x01" * 4800 + b"\x03" * 4800


async def test_first_audio_is_measured_from_the_first_end_of_speech(fake_openai, tmp_path):
    class TwoStops:
        async def on_question(self, index):
            await fake_openai.push({"type": "input_audio_buffer.speech_stopped"})
            await asyncio.sleep(0.3)
            for msg in (
                {"type": "input_audio_buffer.speech_stopped"},
                {"type": "response.created", "response": {"id": "r1"}},
                delta("r1", 4800),
                done("r1", usage=USAGE),
            ):
                await fake_openai.push(msg)

    script(fake_openai, TwoStops())
    [r] = await run_tryout(cfg(fake_openai.url), [SAMPLES / "q1.wav"], tmp_path)
    assert r.turns == 2
    assert r.first_audio_s >= 0.29


async def test_run_tryout_waits_for_the_whole_question_to_be_sent(fake_openai, tmp_path, monkeypatch):
    original = tryout._feed

    async def slow_feed(session, data):
        await original(session, data[: tryout.CHUNK_BYTES])
        await asyncio.sleep(0.3)
        await original(session, data[tryout.CHUNK_BYTES :])

    monkeypatch.setattr(tryout, "_feed", slow_feed)
    script(fake_openai, Plain(fake_openai, [("56", 4800, USAGE)]))
    await run_tryout(cfg(fake_openai.url), [SAMPLES / "q1.wav"], tmp_path)
    chunks = [base64.b64decode(m["audio"]) for m in fake_openai.received[2:]]
    assert b"".join(chunks) == tryout.prepare_question(SAMPLES / "q1.wav")


async def test_run_tryout_fails_on_failed_response(fake_openai, tmp_path):
    class Failing:
        async def on_question(self, index):
            await fake_openai.push(
                {
                    "type": "response.done",
                    "response": {
                        "id": "r1",
                        "status": "failed",
                        "status_details": {"type": "failed", "error": {"code": "insufficient_quota"}},
                        "output": [],
                    },
                }
            )

    script(fake_openai, Failing())
    with pytest.raises(RealtimeError, match="insufficient_quota"):
        await run_tryout(cfg(fake_openai.url), [SAMPLES / "q1.wav"], tmp_path)


async def test_run_tryout_fails_on_server_error(fake_openai, tmp_path):
    class Erroring:
        async def on_question(self, index):
            await fake_openai.push({"type": "error", "error": {"code": "rate_limit_exceeded", "message": "slow down"}})

    script(fake_openai, Erroring())
    with pytest.raises(RealtimeError, match="rate_limit_exceeded.*slow down"):
        await run_tryout(cfg(fake_openai.url), [SAMPLES / "q1.wav"], tmp_path)


async def test_run_tryout_fails_when_server_closes(fake_openai, tmp_path):
    class Closing:
        async def on_question(self, index):
            await fake_openai.close_connection()

    script(fake_openai, Closing())
    with pytest.raises(RealtimeError):
        await run_tryout(cfg(fake_openai.url), [SAMPLES / "q1.wav"], tmp_path)


async def test_run_tryout_times_out_without_answer(fake_openai, tmp_path, monkeypatch):
    monkeypatch.setattr(tryout, "QUESTION_TIMEOUT_S", 0.3)

    class Silent:
        async def on_question(self, index):
            pass

    script(fake_openai, Silent())
    with pytest.raises(RealtimeError, match="no complete answer"):
        await run_tryout(cfg(fake_openai.url), [SAMPLES / "q1.wav"], tmp_path)


async def test_run_tryout_rejects_bad_questions_before_connecting(fake_openai, tmp_path):
    with pytest.raises(OSError):
        await run_tryout(cfg(fake_openai.url), [SAMPLES / "q1.wav", tmp_path / "missing.wav"], tmp_path)
    for content in (b"not a wav", b"junk\n", b""):
        bad = tmp_path / "bad.wav"
        bad.write_bytes(content)
        with pytest.raises(ValueError, match="bad.wav"):
            await run_tryout(cfg(fake_openai.url), [bad], tmp_path)
    assert fake_openai.query == {} and fake_openai.received == []


def test_parse_args():
    args = parse_args(["--model", "m", "--out", "o", "a.wav", "b.wav"])
    assert args.model == "m" and args.out == Path("o") and args.questions == [Path("a.wav"), Path("b.wav")]
    assert parse_args(["--out", "o", "a.wav"]).model is None
    with pytest.raises(SystemExit):
        parse_args(["a.wav"])
    with pytest.raises(SystemExit):
        parse_args(["--out", "o"])


def stub_run(monkeypatch, results=None, error=None):
    seen = {}

    async def fake_run(cfg, questions, out_dir):
        seen.update(cfg=cfg, questions=questions, out_dir=out_dir)
        if error is not None:
            raise error
        return results

    monkeypatch.setattr(tryout, "run_tryout", fake_run)
    return seen


def test_main_requires_api_key(monkeypatch, capsys):
    seen = stub_run(monkeypatch, results=[])
    assert main(["--out", "o", "q1.wav"], env={}) != 0
    captured = capsys.readouterr()
    assert "OPENAI_API_KEY" in captured.err
    assert seen == {}


def test_main_prints_results_and_hides_key(monkeypatch, capsys, tmp_path):
    results = [
        TryoutResult(tmp_path / "answer-1.wav", "П'ятдесят шість", 0.84, USAGE, 1),
        TryoutResult(tmp_path / "answer-2.wav", "", None, {}, 3),
    ]
    seen = stub_run(monkeypatch, results=results)
    code = main(["--model", "gpt-x", "--out", str(tmp_path), "q1.wav", "q4.wav"], env={"OPENAI_API_KEY": "sk-secret-123"})
    captured = capsys.readouterr()
    assert code == 0
    assert seen["cfg"].api_key == "sk-secret-123" and seen["cfg"].model == "gpt-x"
    assert seen["questions"] == [Path("q1.wav"), Path("q4.wav")] and seen["out_dir"] == tmp_path
    assert "П'ятдесят шість" in captured.out
    assert "0.84" in captured.out and "n/a" in captured.out
    assert f"{estimate_cost(USAGE):.6f}" in captured.out
    assert "answer-1.wav" in captured.out and "answer-2.wav" in captured.out
    assert "turns: 1" in captured.out and "turns: 3" in captured.out
    assert captured.out.count("warning") == 1 and "3 turns" in captured.out
    assert "sk-secret-123" not in captured.out + captured.err


def test_main_reports_a_question_ended_by_the_model(monkeypatch, capsys, tmp_path):
    results = [
        TryoutResult(tmp_path / "answer-1.wav", "До зустрічі!", 0.5, USAGE, 1, True),
        TryoutResult(tmp_path / "answer-2.wav", "Так", 0.5, USAGE, 1),
    ]
    stub_run(monkeypatch, results=results)
    assert main(["--out", str(tmp_path), "q1.wav", "q4.wav"], env={"OPENAI_API_KEY": "sk-x"}) == 0
    captured = capsys.readouterr()
    assert captured.out.count("ended by model") == 1
    first, second = captured.out.split("answer-2.wav")
    assert "ended by model" in first and "ended by model" not in second


def test_main_uses_default_model(monkeypatch, capsys, tmp_path):
    seen = stub_run(monkeypatch, results=[])
    assert main(["--out", str(tmp_path), "q1.wav"], env={"OPENAI_API_KEY": "sk-x"}) == 0
    assert seen["cfg"].model == OpenAIConfig(api_key="sk-x").model


@pytest.mark.parametrize("error", [RealtimeError("realtime connect timed out"), FileNotFoundError("q1.wav"), ValueError("q1.wav: bad")])
def test_main_reports_errors(monkeypatch, capsys, tmp_path, error):
    stub_run(monkeypatch, error=error)
    code = main(["--out", str(tmp_path), "q1.wav"], env={"OPENAI_API_KEY": "sk-secret-123"})
    captured = capsys.readouterr()
    assert code != 0
    assert str(error) in captured.err
    assert "sk-secret-123" not in captured.out + captured.err
