import argparse
import asyncio
import sys
import time
from pathlib import Path

import yaml
from aioesphomeapi import (
    APIClient,
    ButtonInfo,
    MediaPlayerEntityState,
    MediaPlayerInfo,
    MediaPlayerState,
    SensorInfo,
    SensorState,
)
from aiohttp import web

from voice_hub.audio import DEVICE_RATE, Resampler, read_wav_pcm, wav_header
from voice_hub.streams import AudioStreams, local_ip

API_PORT = 6053
CHUNK_BYTES = 1024
IDLE_TIMEOUT_S = 60.0
HEAP_SENSORS = ("Heap free", "Heap max block")
STARTED_STATES = (MediaPlayerState.ANNOUNCING, MediaPlayerState.PLAYING)


class Probe:
    def __init__(self) -> None:
        self.t0 = time.monotonic()
        self.command_at: float | None = None
        self.get_at: float | None = None
        self.get_count = 0
        self.get_remote = ""
        self.get_agent = ""
        self.served = 0
        self.eof_at: float | None = None
        self.started_at: float | None = None
        self.idle_at: float | None = None
        self.idle = asyncio.Event()

    def now(self) -> float:
        return time.monotonic() - self.t0

    def log(self, text: str) -> None:
        print(f"[{self.now():7.2f}s] {text}", flush=True)

    @web.middleware
    async def middleware(self, request: web.Request, handler):
        self.get_count += 1
        if self.get_at is None:
            self.get_at = self.now()
            self.get_remote = request.remote or ""
            self.get_agent = request.headers.get("User-Agent", "")
            self.log(f"GET from {self.get_remote} agent={self.get_agent!r}")
        try:
            return await handler(request)
        finally:
            self.log("GET handler finished")

    def instrument(self, stream) -> None:
        original = stream.read
        header_counted = False

        async def counting_read():
            nonlocal header_counted
            if not header_counted:
                header_counted = True
                self.served += len(wav_header(DEVICE_RATE))
            chunk = await original()
            if chunk is None:
                self.eof_at = self.now()
                self.log("handler reached end of stream")
            else:
                self.served += len(chunk)
            return chunk

        stream.read = counting_read


def load_key(secrets: str, name: str) -> str:
    data = yaml.safe_load(Path(secrets).read_text())
    key = data.get(f"{name}_api_key") if isinstance(data, dict) else None
    if not key:
        raise SystemExit(f"{name}_api_key not found in {secrets}")
    return str(key)


async def connect(host: str, secrets: str, name: str):
    client = APIClient(host, API_PORT, None, noise_psk=load_key(secrets, name))
    await client.connect(login=True)
    entities, _ = await client.list_entities_services()
    return client, entities


def prepare_pcm(wav: str) -> bytes:
    rate, pcm = read_wav_pcm(Path(wav))
    if rate == DEVICE_RATE:
        return pcm
    resampler = Resampler(rate, DEVICE_RATE)
    return resampler.process(pcm) + resampler.flush()


async def feed(stream, pcm: bytes, speed: float, stall: float, probe: Probe) -> None:
    bytes_per_s = DEVICE_RATE * 2 * speed
    middle = len(pcm) // 2 // CHUNK_BYTES * CHUNK_BYTES
    start = time.monotonic()
    shift = 0.0
    sent = 0
    stalled = False
    while sent < len(pcm):
        if not stalled and sent >= middle:
            probe.log(f"stall {stall:.2f}s at byte {sent}")
            await asyncio.sleep(stall)
            shift += stall
            stalled = True
        chunk = pcm[sent : sent + CHUNK_BYTES]
        stream.write(chunk)
        sent += len(chunk)
        delay = start + shift + sent / bytes_per_s - time.monotonic()
        if delay > 0:
            await asyncio.sleep(delay)
    stream.finish()
    probe.log(f"finish() after {sent} PCM bytes")


async def play(args: argparse.Namespace) -> int:
    pcm = prepare_pcm(args.wav)
    expected = len(wav_header(DEVICE_RATE)) + len(pcm)
    duration = len(pcm) / (DEVICE_RATE * 2)
    probe = Probe()
    streams = AudioStreams()
    app = web.Application(middlewares=[probe.middleware])
    app.router.add_get("/audio/{stream_id}.wav", streams.handle)
    runner = web.AppRunner(app)
    await runner.setup()
    await web.TCPSite(runner, "0.0.0.0", args.port).start()
    client, entities = await connect(args.host, args.secrets, args.name)
    try:
        info = next((e for e in entities if isinstance(e, MediaPlayerInfo)), None)
        if info is None:
            print("no media player on device")
            return 1
        sensors = {e.key: e.name for e in entities if isinstance(e, SensorInfo) and e.name in HEAP_SENSORS}
        player_seen = asyncio.Event()

        def on_state(state) -> None:
            if isinstance(state, MediaPlayerEntityState) and state.key == info.key:
                probe.log(f"player {state.state.name} volume={state.volume:.2f} muted={state.muted}")
                player_seen.set()
                if probe.command_at is None:
                    return
                if state.state in STARTED_STATES and probe.started_at is None:
                    probe.started_at = probe.now()
                elif state.state == MediaPlayerState.IDLE and probe.started_at is not None and probe.idle_at is None:
                    probe.idle_at = probe.now()
                    probe.idle.set()
            elif isinstance(state, SensorState) and state.key in sensors and not state.missing_state:
                probe.log(f"{sensors[state.key]} = {state.state:.0f}")

        client.subscribe_states(on_state)
        try:
            await asyncio.wait_for(player_seen.wait(), 5.0)
        except asyncio.TimeoutError:
            probe.log("no initial player state")

        ip = local_ip()
        stream = streams.create()
        probe.instrument(stream)
        url = f"http://{ip}:{args.port}/audio/{stream.id}.wav"
        probe.command_at = probe.now()
        probe.log(f"command: announce {duration:.2f}s of audio from {ip}:{args.port}")
        client.media_player_command(info.key, media_url=url, announcement=True, device_id=info.device_id)
        await feed(stream, pcm, args.speed, args.stall, probe)
        remaining = IDLE_TIMEOUT_S - (probe.now() - probe.command_at)
        try:
            await asyncio.wait_for(probe.idle.wait(), max(remaining, 0.1))
        except asyncio.TimeoutError:
            probe.log("timed out waiting for IDLE")
        await asyncio.sleep(1.0)
    finally:
        await client.disconnect()
        await runner.cleanup()

    cmd = probe.command_at or 0.0
    get_latency = None if probe.get_at is None else probe.get_at - cmd
    print()
    print(f"audio: {duration:.2f}s, {len(pcm)} PCM bytes, expected {expected} bytes with header")
    print(f"GET: {'none' if get_latency is None else f'{get_latency:.2f}s after command'} (requests: {probe.get_count})")
    print(f"served: {probe.served} / {expected} bytes, end of stream reached: {probe.eof_at is not None}")
    print(f"ANNOUNCING/PLAYING at: {'never' if probe.started_at is None else f'{probe.started_at - cmd:.2f}s after command'}")
    print(f"IDLE at: {'never' if probe.idle_at is None else f'{probe.idle_at - cmd:.2f}s after command'}")
    ok = (
        probe.get_at is not None
        and probe.served == expected
        and probe.started_at is not None
        and probe.idle_at is not None
        and probe.idle_at > probe.started_at
        and probe.idle_at - cmd <= IDLE_TIMEOUT_S
    )
    print("RESULT:", "OK" if ok else "FAIL")
    return 0 if ok else 1


async def listen(args: argparse.Namespace) -> int:
    client, entities = await connect(args.host, args.secrets, args.name)
    try:
        info = next((e for e in entities if isinstance(e, ButtonInfo) and e.name == "Listen"), None)
        if info is None:
            print("no Listen button on device")
            return 1
        client.button_command(info.key, device_id=info.device_id)
        await asyncio.sleep(0.3)
        print("Listen pressed")
        return 0
    finally:
        await client.disconnect()


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="devtools.devcheck")
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("play", "listen"):
        p = sub.add_parser(name)
        p.add_argument("--host", required=True)
        p.add_argument("--secrets", required=True)
        p.add_argument("--name", default="kitchen")
        if name == "play":
            p.add_argument("--wav", required=True)
            p.add_argument("--speed", type=float, default=2.0)
            p.add_argument("--stall", type=float, default=1.5)
            p.add_argument("--port", type=int, default=8080)
    return parser.parse_args(argv)


def main() -> None:
    args = parse_args()
    handler = play if args.command == "play" else listen
    sys.exit(asyncio.run(handler(args)))


if __name__ == "__main__":
    main()
