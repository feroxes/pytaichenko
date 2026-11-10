import math
import wave

import numpy as np
import pytest

from voice_hub.audio import DEVICE_RATE, OPENAI_RATE, HighPass, Loudness, Resampler, read_wav_pcm, wav_header


def sine(rate, seconds=1.0, freq=440.0):
    t = np.arange(int(rate * seconds)) / rate
    return (np.sin(2 * np.pi * freq * t) * 10000).astype("<i2").tobytes()


def run_chunked(in_rate, out_rate, pcm, chunk_samples=512):
    r = Resampler(in_rate, out_rate)
    step = chunk_samples * 2
    out = b"".join(r.process(pcm[i : i + step]) for i in range(0, len(pcm), step))
    return out + r.flush()


def test_rates():
    assert DEVICE_RATE == 16000 and OPENAI_RATE == 24000


def test_wav_header_streaming():
    h = wav_header(16000)
    assert h == (
        b"RIFF" + b"\xff\xff\xff\xff" + b"WAVE" + b"fmt " + (16).to_bytes(4, "little")
        + (1).to_bytes(2, "little") + (1).to_bytes(2, "little") + (16000).to_bytes(4, "little")
        + (32000).to_bytes(4, "little") + (2).to_bytes(2, "little") + (16).to_bytes(2, "little")
        + b"data" + b"\xff\xff\xff\xff"
    )


def test_wav_header_other_rate():
    h = wav_header(24000)
    assert len(h) == 44
    assert int.from_bytes(h[24:28], "little") == 24000
    assert int.from_bytes(h[28:32], "little") == 48000


def test_resample_16k_to_24k_length():
    out = run_chunked(16000, 24000, sine(16000))
    assert abs(len(out) // 2 - 24000) <= 2


def test_resample_24k_to_16k_length():
    out = run_chunked(24000, 16000, sine(24000))
    assert abs(len(out) // 2 - 16000) <= 2


def test_resample_chunked_equals_whole():
    pcm = sine(16000)
    chunked = run_chunked(16000, 24000, pcm)
    whole = run_chunked(16000, 24000, pcm, chunk_samples=len(pcm) // 2)
    assert abs(len(chunked) - len(whole)) // 2 <= 2


def test_resample_preserves_frequency():
    out = np.frombuffer(run_chunked(16000, 24000, sine(16000)), "<i2").astype(float)
    spectrum = np.abs(np.fft.rfft(out))
    peak_hz = np.argmax(spectrum) * OPENAI_RATE / len(out)
    assert abs(peak_hz - 440) < 5


def test_resample_rejects_odd_bytes():
    with pytest.raises(ValueError):
        Resampler(16000, 24000).process(b"\x00")


def test_read_wav_pcm(tmp_path):
    frames = sine(16000, 0.1)
    p = tmp_path / "a.wav"
    with wave.open(str(p), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(16000)
        w.writeframes(frames)
    assert read_wav_pcm(p) == (16000, frames)


def test_read_wav_rejects_stereo(tmp_path):
    p = tmp_path / "s.wav"
    with wave.open(str(p), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(16000)
        w.writeframes(b"\x00\x00" * 200)
    with pytest.raises(ValueError):
        read_wav_pcm(p)


def test_read_wav_rejects_8bit(tmp_path):
    p = tmp_path / "e.wav"
    with wave.open(str(p), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(1)
        w.setframerate(16000)
        w.writeframes(b"\x00" * 100)
    with pytest.raises(ValueError):
        read_wav_pcm(p)


def rms(pcm: bytes, skip: int = 0) -> float:
    a = np.frombuffer(pcm, "<i2").astype(float)[skip:]
    return float(np.sqrt(np.mean(a**2)))


def test_high_pass_removes_drift_and_hum():
    hp = HighPass(DEVICE_RATE)
    assert rms(hp.process(sine(DEVICE_RATE, 2.0, 3.0)), 8000) < rms(sine(DEVICE_RATE, 2.0, 3.0)) / 1000
    hp = HighPass(DEVICE_RATE)
    assert rms(hp.process(sine(DEVICE_RATE, 1.0, 180.0)), 4000) < rms(sine(DEVICE_RATE, 1.0, 180.0)) / 3


def test_high_pass_keeps_speech_band():
    for freq in (500.0, 1000.0, 3000.0):
        hp = HighPass(DEVICE_RATE)
        kept = rms(hp.process(sine(DEVICE_RATE, 1.0, freq)), 4000) / rms(sine(DEVICE_RATE, 1.0, freq))
        assert 0.85 < kept < 1.1


def test_high_pass_chunked_equals_whole():
    pcm = sine(DEVICE_RATE, 1.0, 700.0)
    whole = HighPass(DEVICE_RATE).process(pcm)
    hp = HighPass(DEVICE_RATE)
    chunked = b"".join(hp.process(pcm[i : i + 1024]) for i in range(0, len(pcm), 1024))
    assert chunked == whole


def test_high_pass_clips_instead_of_wrapping():
    loud = np.full(1600, 32767, "<i2")
    loud[800:] = -32768
    out = np.frombuffer(HighPass(DEVICE_RATE).process(loud.tobytes()), "<i2")
    assert out.min() >= -32768 and out.max() <= 32767
    assert out[801] < 0


def test_loudness_raises_speech_by_six_db():
    quiet = sine(DEVICE_RATE, 1.0, 1000.0)
    gained = rms(Loudness(DEVICE_RATE).process(quiet), 4000) / rms(quiet, 4000)
    assert 1.9 < gained < 2.1


def test_loudness_limits_peaks_without_hard_clipping():
    t = np.arange(DEVICE_RATE) / DEVICE_RATE
    loud = (np.sin(2 * np.pi * 1000 * t) * 32767).astype("<i2")
    out = np.abs(np.frombuffer(Loudness(DEVICE_RATE).process(loud.tobytes()), "<i2").astype(int))
    assert out.max() < 32767
    assert out[4000:].max() > 0.95 * 32767


def test_loudness_cuts_bass():
    bass = sine(DEVICE_RATE, 1.0, 50.0)
    assert rms(Loudness(DEVICE_RATE).process(bass), 4000) < rms(bass, 4000) / 10


def test_loudness_chunked_equals_whole():
    pcm = sine(DEVICE_RATE, 1.0, 700.0)
    whole = Loudness(DEVICE_RATE).process(pcm)
    boost = Loudness(DEVICE_RATE)
    chunked = b"".join(boost.process(pcm[i : i + 1024]) for i in range(0, len(pcm), 1024))
    assert chunked == whole


class ReferenceHighPass:
    def __init__(self, rate: int, cutoff_hz: float = 250.0) -> None:
        self._sections = [self._coeffs(rate, cutoff_hz, q) for q in (0.5412, 1.3066)]
        self._state = [[0.0, 0.0, 0.0, 0.0] for _ in self._sections]

    @staticmethod
    def _coeffs(rate: int, cutoff_hz: float, q: float) -> tuple[float, float, float, float, float]:
        w0 = 2 * math.pi * cutoff_hz / rate
        alpha = math.sin(w0) / (2 * q)
        cos_w0 = math.cos(w0)
        a0 = 1 + alpha
        b0 = (1 + cos_w0) / 2 / a0
        return b0, -2 * b0, b0, -2 * cos_w0 / a0, (1 - alpha) / a0

    def process(self, pcm: bytes) -> bytes:
        samples = np.frombuffer(pcm, "<i2").astype(np.float64).tolist()
        for (b0, b1, b2, a1, a2), state in zip(self._sections, self._state):
            x1, x2, y1, y2 = state
            for i, x in enumerate(samples):
                y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
                x2, x1, y2, y1 = x1, x, y1, y
                samples[i] = y
            state[:] = [x1, x2, y1, y2]
        return np.clip(np.rint(samples), -32768, 32767).astype("<i2").tobytes()


class ReferenceLoudness:
    def __init__(self, rate: int, gain_db: float = 6.0, cutoff_hz: float = 150.0, knee: float = 0.7) -> None:
        self._filter = ReferenceHighPass(rate, cutoff_hz)
        self._gain = 10 ** (gain_db / 20)
        self._knee = knee

    def process(self, pcm: bytes) -> bytes:
        x = np.frombuffer(self._filter.process(pcm), "<i2").astype(np.float64) / 32768 * self._gain
        level = np.abs(x)
        over = level > self._knee
        room = 1 - self._knee
        x[over] = np.sign(x[over]) * (self._knee + room * np.tanh((level[over] - self._knee) / room))
        return np.clip(np.rint(x * 32767), -32768, 32767).astype("<i2").tobytes()


UNEVEN_CHUNKS = (1, 0, 2, 3, 7, 127, 128, 129, 255, 256, 257, 511, 1000, 4096, 1, 9000)


def mixed_signal() -> bytes:
    rng = np.random.default_rng(7)
    t = np.arange(3 * DEVICE_RATE) / DEVICE_RATE
    x = rng.normal(0, 4000, t.size) + 12000 * np.sin(2 * np.pi * 3 * t) + 6000 * np.sin(2 * np.pi * 180 * t)
    x += 5000 * np.sin(2 * np.pi * 1000 * t)
    x[20000:21000] = 32767
    x[21000:22000] = -32768
    return np.clip(np.rint(x), -32768, 32767).astype("<i2").tobytes()


def in_chunks(processor, pcm: bytes) -> bytes:
    out, start, index = [], 0, 0
    while start < len(pcm):
        size = UNEVEN_CHUNKS[index % len(UNEVEN_CHUNKS)] * 2
        out.append(processor.process(pcm[start : start + size]))
        start += size
        index += 1
    return b"".join(out)


def max_diff(a: bytes, b: bytes) -> int:
    assert len(a) == len(b)
    return int(np.abs(np.frombuffer(a, "<i2").astype(np.int32) - np.frombuffer(b, "<i2").astype(np.int32)).max())


@pytest.mark.parametrize("cutoff_hz", [250.0, 150.0])
def test_highpass_matches_reference(cutoff_hz):
    pcm = mixed_signal()
    reference = ReferenceHighPass(DEVICE_RATE, cutoff_hz).process(pcm)
    whole = HighPass(DEVICE_RATE, cutoff_hz).process(pcm)
    chunked = in_chunks(HighPass(DEVICE_RATE, cutoff_hz), pcm)
    assert max_diff(whole, reference) <= 1
    assert max_diff(chunked, reference) <= 1
    assert max_diff(chunked, whole) <= 1
    assert HighPass(DEVICE_RATE, cutoff_hz).process(b"") == b""


def test_loudness_matches_reference():
    pcm = mixed_signal()
    reference = ReferenceLoudness(DEVICE_RATE).process(pcm)
    whole = Loudness(DEVICE_RATE).process(pcm)
    chunked = in_chunks(Loudness(DEVICE_RATE), pcm)
    assert max_diff(whole, reference) <= 1
    assert max_diff(chunked, reference) <= 1
    assert max_diff(chunked, whole) <= 1
    assert Loudness(DEVICE_RATE).process(b"") == b""
