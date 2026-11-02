import argparse
import asyncio
import json
import os
import sys
import time
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, NamedTuple

import aiohttp

from voice_hub.audio import OPENAI_RATE, Resampler, read_wav_pcm
from voice_hub.config import OpenAIConfig
from voice_hub.realtime import (
    AudioDelta,
    Closed,
    RealtimeError,
    RealtimeSession,
    ResponseCreated,
    ResponseDone,
    ServerError,
    SpeechStarted,
    SpeechStopped,
    ToolCall,
    TranscriptDone,
)
from voice_hub.tools import END_TOOL_NAME, VOLUME_TOOL_NAME

CHUNK_MS = 100
CHUNK_BYTES = OPENAI_RATE * 2 * CHUNK_MS // 1000
SILENCE_S = 2
SILENCE = bytes(OPENAI_RATE * 2 * SILENCE_S)
QUESTION_TIMEOUT_S = 60.0
QUIET_GRACE_S = 1.5
TOOL_VOLUME_PERCENT = 55

AUDIO_IN_PER_M = 10.0
AUDIO_IN_CACHED_PER_M = 0.30
AUDIO_OUT_PER_M = 20.0
TEXT_IN_PER_M = 0.60
TEXT_IN_CACHED_PER_M = 0.06
TEXT_OUT_PER_M = 2.40


@dataclass(frozen=True)
class TryoutResult:
    answer_path: Path
    transcript: str
    first_audio_s: float | None
    usage: dict
    turns: int
    ended: bool = False


class Tokens(NamedTuple):
    audio_in: float
    audio_cached: float
    audio_out: float
    text_in: float
    text_cached: float
    text_out: float


def _number(value: object) -> float:
    return value if isinstance(value, (int, float)) and not isinstance(value, bool) else 0


def _section(data: dict, key: str) -> dict:
    value = data.get(key)
    return value if isinstance(value, dict) else {}


def tokens_of(usage: dict) -> Tokens:
    inputs = _section(usage, "input_token_details")
    cached = _section(inputs, "cached_tokens_details")
    outputs = _section(usage, "output_token_details")
    return Tokens(
        audio_in=_number(inputs.get("audio_tokens")),
        audio_cached=_number(cached.get("audio_tokens")),
        audio_out=_number(outputs.get("audio_tokens")),
        text_in=_number(inputs.get("text_tokens")),
        text_cached=_number(cached.get("text_tokens")),
        text_out=_number(outputs.get("text_tokens")),
    )


def estimate_cost(usage: dict) -> float:
    t = tokens_of(usage)
    dollars = (
        max(0, t.audio_in - t.audio_cached) * AUDIO_IN_PER_M
        + t.audio_cached * AUDIO_IN_CACHED_PER_M
        + t.audio_out * AUDIO_OUT_PER_M
        + max(0, t.text_in - t.text_cached) * TEXT_IN_PER_M
        + t.text_cached * TEXT_IN_CACHED_PER_M
        + t.text_out * TEXT_OUT_PER_M
    )
    return dollars / 1_000_000


def _add_usage(total: dict, usage: dict) -> dict:
    for key, value in usage.items():
        if isinstance(value, dict):
            nested = total.get(key)
            total[key] = _add_usage(nested if isinstance(nested, dict) else {}, value)
        elif isinstance(value, (int, float)) and not isinstance(value, bool):
            total[key] = _number(total.get(key)) + value
    return total


def prepare_question(path: Path) -> bytes:
    try:
        rate, pcm = read_wav_pcm(path)
    except EOFError as e:
        raise ValueError(f"{path}: truncated or empty WAV file") from e
    if rate != OPENAI_RATE:
        resampler = Resampler(rate, OPENAI_RATE)
        pcm = resampler.process(pcm) + resampler.flush()
    return pcm + SILENCE


def write_wav(path: Path, pcm: bytes) -> None:
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(OPENAI_RATE)
        w.writeframes(pcm)


def _tool_output(call: ToolCall) -> str:
    if call.name == VOLUME_TOOL_NAME:
        return json.dumps({"volume": TOOL_VOLUME_PERCENT})
    if call.name == END_TOOL_NAME:
        return json.dumps({"ok": True})
    return json.dumps({"error": f"unknown tool {call.name}"})


@dataclass(frozen=True)
class _Answer:
    pcm: bytes
    transcript: str
    first_audio_s: float | None
    usage: dict
    turns: int
    ended: bool


async def _feed(session: RealtimeSession, data: bytes) -> None:
    for start in range(0, len(data), CHUNK_BYTES):
        await session.append_audio(data[start : start + CHUNK_BYTES])
        await asyncio.sleep(0)


async def _collect(session: RealtimeSession, feeder: asyncio.Task) -> _Answer:
    audio = bytearray()
    transcripts: list[str] = []
    usage: dict = {}
    first_stopped_at: float | None = None
    first_audio_s: float | None = None
    turns = 0
    in_flight: set[str] = set()
    in_speech = False
    tool_pending = False
    answered = False
    ended = False
    last_event_at = time.monotonic()
    while not ended:
        if answered and not in_flight and not tool_pending and not in_speech:
            was_sending = not feeder.done()
            await feeder
            if was_sending:
                last_event_at = time.monotonic()
            remaining = last_event_at + QUIET_GRACE_S - time.monotonic()
            if remaining <= 0:
                break
            try:
                async with asyncio.timeout(remaining):
                    event = await session.next_event()
            except TimeoutError:
                break
        else:
            event = await session.next_event()
        last_event_at = time.monotonic()
        if isinstance(event, SpeechStarted):
            in_speech = True
        elif isinstance(event, SpeechStopped):
            in_speech = False
            turns += 1
            if first_stopped_at is None:
                first_stopped_at = last_event_at
        elif isinstance(event, ResponseCreated):
            in_flight.add(event.response_id)
            tool_pending = False
        elif isinstance(event, AudioDelta):
            if not audio and first_stopped_at is not None:
                first_audio_s = last_event_at - first_stopped_at
            audio += event.pcm
        elif isinstance(event, TranscriptDone):
            transcripts.append(event.text)
        elif isinstance(event, ResponseDone):
            in_flight.discard(event.response_id)
            _add_usage(usage, event.usage)
            if event.status == "failed":
                raise RealtimeError(f"response failed: {event.error or 'unknown error'}")
            if event.tool_calls:
                ended = any(call.name == END_TOOL_NAME for call in event.tool_calls)
                outputs = [(call.call_id, _tool_output(call)) for call in event.tool_calls]
                await session.send_tool_outputs(outputs, respond=not ended)
                tool_pending = not ended
            else:
                answered = True
        elif isinstance(event, ServerError):
            raise RealtimeError(f"server error {event.code}: {event.message}")
        elif isinstance(event, Closed):
            raise RealtimeError("connection closed before the answer was complete")
    return _Answer(bytes(audio), " ".join(transcripts), first_audio_s, usage, turns, ended)


async def _ask(session: RealtimeSession, data: bytes) -> _Answer:
    await session.clear_input()
    feeder = asyncio.create_task(_feed(session, data))
    try:
        async with asyncio.timeout(QUESTION_TIMEOUT_S):
            return await _collect(session, feeder)
    except TimeoutError as e:
        raise RealtimeError(f"no complete answer within {QUESTION_TIMEOUT_S:g}s") from e
    finally:
        feeder.cancel()
        await asyncio.gather(feeder, return_exceptions=True)


async def run_tryout(cfg: OpenAIConfig, questions: list[Path], out_dir: Path) -> list[TryoutResult]:
    prepared = [prepare_question(path) for path in questions]
    out_dir.mkdir(parents=True, exist_ok=True)
    results: list[TryoutResult] = []
    async with aiohttp.ClientSession() as http:
        session = RealtimeSession(cfg, http)
        await session.connect()
        try:
            for number, data in enumerate(prepared, 1):
                answer = await _ask(session, data)
                path = out_dir / f"answer-{number}.wav"
                write_wav(path, answer.pcm)
                results.append(
                    TryoutResult(path, answer.transcript, answer.first_audio_s, answer.usage, answer.turns, answer.ended)
                )
        finally:
            await session.close()
    return results


def format_tokens(usage: dict) -> str:
    t = tokens_of(usage)
    return (
        f"audio in {t.audio_in:g} (cached {t.audio_cached:g}), text in {t.text_in:g} (cached {t.text_cached:g}), "
        f"audio out {t.audio_out:g}, text out {t.text_out:g}"
    )


def report(questions: list[Path], results: list[TryoutResult]) -> None:
    total = 0.0
    for question, result in zip(questions, results):
        cost = estimate_cost(result.usage)
        total += cost
        latency = "n/a" if result.first_audio_s is None else f"{result.first_audio_s:.2f} s after end of speech"
        print(f"{question.name} -> {result.answer_path}")
        print(f"  transcript: {result.transcript}")
        print(f"  first audio: {latency}")
        print(f"  turns: {result.turns}")
        if result.ended:
            print("  ended by model")
        if result.turns > 1:
            print(
                f"  warning: the server split this question into {result.turns} turns "
                f"(a pause longer than silence_ms); the answer joins all of them"
            )
        print(f"  tokens: {format_tokens(result.usage)}")
        print(f"  cost: ${cost:.6f}")
    if len(results) > 1:
        print(f"total cost: ${total:.6f}")
    print("cost is estimated at gpt-realtime-mini prices")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="devtools.tryout")
    parser.add_argument("--model")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("questions", type=Path, nargs="+")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None, env: Mapping[str, str] | None = None) -> int:
    args = parse_args(argv)
    env = os.environ if env is None else env
    key = env.get("OPENAI_API_KEY")
    if not key:
        print("error: environment variable OPENAI_API_KEY is not set", file=sys.stderr)
        return 1
    options = {"model": args.model} if args.model else {}
    cfg = OpenAIConfig(api_key=key, **options)
    try:
        results = asyncio.run(run_tryout(cfg, args.questions, args.out))
    except (RealtimeError, OSError, ValueError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    report(args.questions, results)
    return 0


if __name__ == "__main__":
    sys.exit(main())
