import math
import struct
import wave
from pathlib import Path

RATE = 16000
ATTACK = 0.004
FADE = 0.03
C6 = 1046.5
G6 = 1568.0
PARTIALS = [(1, 1.0, 1.0), (2, 0.25, 0.6), (3, 0.08, 0.4)]


def bell(freq, start, length, decay):
    tones = []
    for n in range(round(RATE * length)):
        t = n / RATE
        value = sum(amp * math.sin(2 * math.pi * ratio * freq * (n + 1) / RATE) * math.exp(-t / (decay * tau))
                    for ratio, amp, tau in PARTIALS)
        tones.append(value * min(1.0, t / ATTACK))
    return round(RATE * start), tones


def mix(notes, length, peak_db):
    buf = [0.0] * round(RATE * length)
    for offset, tones in notes:
        for i, value in enumerate(tones[: len(buf) - offset]):
            buf[offset + i] += value
    fade = round(RATE * FADE)
    for i in range(fade):
        buf[len(buf) - fade + i] *= 1 - i / fade
    scale = 32767 * 10 ** (peak_db / 20) / max(abs(v) for v in buf)
    return [round(v * scale) for v in buf]


def write(path, samples):
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(b"".join(struct.pack("<h", s) for s in samples))


def main():
    folder = Path(__file__).resolve().parent
    write(folder / "wake.wav", mix([bell(C6, 0, 0.3, 0.11), bell(G6, 0.08, 0.22, 0.11)], 0.3, -14))
    write(folder / "end.wav", mix([bell(G6, 0, 0.4, 0.13), bell(C6, 0.1, 0.3, 0.13)], 0.4, -16))


if __name__ == "__main__":
    main()
