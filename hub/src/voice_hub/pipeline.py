import asyncio
import math
from collections.abc import Callable

import numpy as np

from voice_hub.audio import DEVICE_RATE, OPENAI_RATE, HighPass, Loudness, Resampler

DEVICE_BYTES_PER_S = DEVICE_RATE * 2
DEBUG_CHUNK = 1024
DEBUG_CHUNK_S = DEBUG_CHUNK / DEVICE_BYTES_PER_S


class AudioPath:
    def __init__(self, holdoff_s: float, raw: bool = False) -> None:
        self._holdoff_s = holdoff_s
        self._raw = raw
        self._mic_filter = HighPass(DEVICE_RATE)
        self._mic = Resampler(DEVICE_RATE, OPENAI_RATE)
        self._speaker = Resampler(OPENAI_RATE, DEVICE_RATE)
        self._boost = Loudness(DEVICE_RATE)
        self._mic_samples = 0
        self._mic_peak = 0

    def from_mic(self, pcm: bytes) -> bytes:
        if self._raw:
            self._mic_samples += len(pcm) // 2
            return self._to_openai(pcm)
        pcm = pcm[: len(pcm) // 2 * 2]
        self._mic_samples += len(pcm) // 2
        if self._mic_samples <= self._holdoff_s * DEVICE_RATE:
            return b""
        return self._to_openai(self._mic_filter.process(pcm))

    def to_device(self, pcm: bytes) -> bytes:
        return self._boost.process(self._speaker.process(pcm))

    def flush(self) -> bytes:
        return self._boost.process(self._speaker.flush())

    def mic_summary(self) -> str:
        level = f"{20 * math.log10(self._mic_peak / 32768):.1f} dBFS" if self._mic_peak else "silence"
        return f"{self._mic_samples / DEVICE_RATE:.2f} s, peak {self._mic_peak} ({level})"

    def _to_openai(self, pcm: bytes) -> bytes:
        samples = np.frombuffer(pcm, dtype=np.int16)
        if samples.size:
            self._mic_peak = max(self._mic_peak, int(np.abs(samples.astype(np.int32)).max()))
        return self._mic.process(pcm)


async def feed_debug(data: bytes, push: Callable[[bytes], None], active: Callable[[], bool]) -> None:
    loop = asyncio.get_running_loop()
    start = loop.time()
    index = 0
    while active():
        chunk = data[index * DEBUG_CHUNK : (index + 1) * DEBUG_CHUNK]
        push(chunk + bytes(DEBUG_CHUNK - len(chunk)))
        index += 1
        await asyncio.sleep(max(0.0, start + index * DEBUG_CHUNK_S - loop.time()))
