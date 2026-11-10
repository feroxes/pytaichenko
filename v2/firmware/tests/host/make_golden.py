import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "v1" / "hub" / "src"))

from voice_hub.audio import HighPass, Loudness, Resampler

OUT = Path(__file__).resolve().parent / "golden"
OUT.mkdir(exist_ok=True)


def sine(rate: int, seconds: int, amplitude: float) -> np.ndarray:
    t = np.arange(rate * seconds) / rate
    return np.rint(amplitude * np.sin(2 * np.pi * 1000 * t)).astype("<i2")


def noise(rate: int, seconds: int, sigma: float, seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    return np.clip(np.rint(rng.normal(0, sigma, rate * seconds)), -32768, 32767).astype("<i2")


def save(name: str, data: bytes) -> None:
    (OUT / f"{name}.raw").write_bytes(data)


def resample(signal: np.ndarray, in_rate: int, out_rate: int) -> bytes:
    r = Resampler(in_rate, out_rate)
    return r.process(signal.tobytes()) + r.flush()


inputs16 = {"sine16": sine(16000, 1, 20000), "noise16": noise(16000, 1, 4000, 1)}
inputs24 = {"sine24": sine(24000, 1, 20000), "noise24": noise(24000, 1, 4000, 1)}

for name, signal in {**inputs16, **inputs24}.items():
    save(f"in_{name}", signal.tobytes())

for name, signal in inputs16.items():
    save(f"hp_{name}", HighPass(16000, 250.0).process(signal.tobytes()))
    save(f"loud_{name}", Loudness(16000, 6.0, 150.0).process(signal.tobytes()))
    save(f"up_{name}", resample(signal, 16000, 24000))

for name, signal in inputs24.items():
    save(f"down_{name}", resample(signal, 24000, 16000))
