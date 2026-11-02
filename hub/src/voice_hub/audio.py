import math
import wave
from pathlib import Path

import numpy as np
import soxr

DEVICE_RATE = 16000
OPENAI_RATE = 24000


def wav_header(sample_rate: int) -> bytes:
    return (
        b"RIFF" + b"\xff\xff\xff\xff" + b"WAVE" + b"fmt " + (16).to_bytes(4, "little")
        + (1).to_bytes(2, "little") + (1).to_bytes(2, "little") + sample_rate.to_bytes(4, "little")
        + (sample_rate * 2).to_bytes(4, "little") + (2).to_bytes(2, "little") + (16).to_bytes(2, "little")
        + b"data" + b"\xff\xff\xff\xff"
    )


class Resampler:
    def __init__(self, in_rate: int, out_rate: int) -> None:
        self._stream = soxr.ResampleStream(in_rate, out_rate, 1, dtype="int16")

    def process(self, pcm: bytes) -> bytes:
        if len(pcm) % 2:
            raise ValueError("PCM16 data must have an even number of bytes")
        out = self._stream.resample_chunk(np.frombuffer(pcm, "<i2"))
        return out.astype("<i2", copy=False).tobytes()

    def flush(self) -> bytes:
        out = self._stream.resample_chunk(np.zeros(0, "<i2"), last=True)
        return out.astype("<i2", copy=False).tobytes()


class IIR:
    def __init__(self, b: tuple[float, ...], a: tuple[float, ...], block: int = 128) -> None:
        order = len(a) - 1
        feedback = np.array([*b[1:], *(-c for c in a[1:])])
        step = np.zeros((2 * order, 2 * order))
        step[1:order, : order - 1] = np.eye(order - 1)
        step[order] = feedback
        step[order + 1 :, order:-1] = np.eye(order - 1)
        drive = np.zeros(2 * order)
        drive[[0, order]] = 1.0, b[0]
        free = [feedback]
        for _ in range(block - 1):
            free.append(free[-1] @ step)
        self._free = np.array(free)
        impulse = np.concatenate(([b[0]], self._free[:-1] @ drive))
        lag = np.subtract.outer(np.arange(block), np.arange(block))
        self._toeplitz = np.where(lag >= 0, impulse[np.maximum(lag, 0)], 0.0)
        self._jump = np.vstack((np.zeros((order, 2 * order)), self._free[: -order - 1 : -1]))
        self._order = order
        self._block = block
        self._state = np.zeros(2 * order)

    def process(self, x: np.ndarray) -> np.ndarray:
        order = self._order
        blocks = np.zeros(-(-x.size // self._block) * self._block)
        blocks[: x.size] = x
        blocks = blocks.reshape(-1, self._block)
        forced = blocks @ self._toeplitz.T
        known = np.hstack((blocks[:, : -order - 1 : -1], forced[:, : -order - 1 : -1]))
        starts = np.empty((len(blocks), 2 * order))
        state = self._state
        for k in range(len(blocks)):
            starts[k] = state
            state = known[k] + self._jump @ state
        y = (forced + starts @ self._free.T).ravel()[: x.size]
        self._state = np.concatenate((
            np.concatenate((x[: -order - 1 : -1], self._state[:order]))[:order],
            np.concatenate((y[: -order - 1 : -1], self._state[order:]))[:order],
        ))
        return y


class HighPass:
    def __init__(self, rate: int, cutoff_hz: float = 250.0) -> None:
        self._sections = []
        for q in (0.5412, 1.3066):
            b0, b1, b2, a1, a2 = self._coeffs(rate, cutoff_hz, q)
            self._sections.append(IIR((b0, b1, b2), (1.0, a1, a2)))

    @staticmethod
    def _coeffs(rate: int, cutoff_hz: float, q: float) -> tuple[float, float, float, float, float]:
        w0 = 2 * math.pi * cutoff_hz / rate
        alpha = math.sin(w0) / (2 * q)
        cos_w0 = math.cos(w0)
        a0 = 1 + alpha
        b0 = (1 + cos_w0) / 2 / a0
        return b0, -2 * b0, b0, -2 * cos_w0 / a0, (1 - alpha) / a0

    def process(self, pcm: bytes) -> bytes:
        samples = np.frombuffer(pcm, "<i2").astype(np.float64)
        for section in self._sections:
            samples = section.process(samples)
        return np.clip(np.rint(samples), -32768, 32767).astype("<i2").tobytes()


class Loudness:
    def __init__(self, rate: int, gain_db: float = 6.0, cutoff_hz: float = 150.0, knee: float = 0.7) -> None:
        self._filter = HighPass(rate, cutoff_hz)
        self._gain = 10 ** (gain_db / 20)
        self._knee = knee

    def process(self, pcm: bytes) -> bytes:
        x = np.frombuffer(self._filter.process(pcm), "<i2").astype(np.float64) / 32768 * self._gain
        level = np.abs(x)
        over = level > self._knee
        room = 1 - self._knee
        x[over] = np.sign(x[over]) * (self._knee + room * np.tanh((level[over] - self._knee) / room))
        return np.clip(np.rint(x * 32767), -32768, 32767).astype("<i2").tobytes()


def read_wav_pcm(path: Path) -> tuple[int, bytes]:
    try:
        with wave.open(str(path), "rb") as w:
            if w.getnchannels() != 1 or w.getsampwidth() != 2:
                raise ValueError(f"{path}: only 16-bit mono WAV is supported")
            return w.getframerate(), w.readframes(w.getnframes())
    except wave.Error as e:
        raise ValueError(f"{path}: {e}") from e
