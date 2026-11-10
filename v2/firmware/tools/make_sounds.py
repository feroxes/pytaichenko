import sys
import wave
from pathlib import Path

import numpy as np
import soxr

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "v1" / "firmware" / "sounds"
TARGET = Path(__file__).resolve().parents[1] / "components" / "pt_audio" / "sounds"
OUT_RATE = 24000
NAMES = ("wake", "end")
GAIN_DB = 8.0


def convert(name):
    with wave.open(str(SOURCE / f"{name}.wav"), "rb") as w:
        if w.getnchannels() != 1 or w.getsampwidth() != 2:
            sys.exit(f"{name}.wav must be mono 16-bit")
        rate = w.getframerate()
        pcm = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2")
    stream = soxr.ResampleStream(rate, OUT_RATE, 1, dtype="int16")
    out = np.concatenate([stream.resample_chunk(pcm), stream.resample_chunk(np.zeros(0, dtype="int16"), last=True)])
    out = np.clip(np.rint(out.astype(np.float64) * 10 ** (GAIN_DB / 20)), -32768, 32767)
    (TARGET / f"{name}.raw").write_bytes(out.astype("<i2").tobytes())
    print(f"{name}: {rate} Hz, {len(pcm)} samples -> {OUT_RATE} Hz, {len(out)} samples")


def main():
    TARGET.mkdir(parents=True, exist_ok=True)
    for name in NAMES:
        convert(name)


main()
