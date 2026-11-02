import asyncio
import socket
import time
import uuid

from aiohttp import web

from voice_hub.audio import DEVICE_RATE, wav_header


def local_ip() -> str:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.connect(("192.0.2.1", 9))
        return sock.getsockname()[0]


class AudioStream:
    def __init__(self) -> None:
        self.id = uuid.uuid4().hex
        self.finished = False
        self.written = 0
        self.finished_at: float | None = None
        self._queue: asyncio.Queue[bytes | None] = asyncio.Queue()

    def write(self, pcm: bytes) -> None:
        if self.finished or not pcm:
            return
        self.written += len(pcm)
        self._queue.put_nowait(pcm)

    def finish(self) -> None:
        if self.finished:
            return
        self.finished = True
        self.finished_at = time.monotonic()
        self._queue.put_nowait(None)

    async def read(self) -> bytes | None:
        return await self._queue.get()


class AudioStreams:
    def __init__(self, ttl_s: float = 120.0) -> None:
        self._ttl_s = ttl_s
        self._streams: dict[str, AudioStream] = {}

    def create(self) -> AudioStream:
        now = time.monotonic()
        for stream_id in [i for i, s in self._streams.items()
                          if s.finished_at is not None and now - s.finished_at > self._ttl_s]:
            del self._streams[stream_id]
        stream = AudioStream()
        self._streams[stream.id] = stream
        return stream

    def discard(self, stream: AudioStream) -> None:
        stream.finish()
        self._streams.pop(stream.id, None)

    async def handle(self, request: web.Request) -> web.StreamResponse:
        stream = self._streams.pop(request.match_info["stream_id"], None)
        if stream is None:
            raise web.HTTPNotFound()
        resp = web.StreamResponse(status=200, headers={"Content-Type": "audio/wav"})
        resp.enable_chunked_encoding()
        await resp.prepare(request)
        try:
            await resp.write(wav_header(DEVICE_RATE))
            while (chunk := await stream.read()) is not None:
                await resp.write(chunk)
            await resp.write_eof()
        except ConnectionResetError:
            pass
        return resp
