import platform
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from pymicro_features import MicroFrontend
from pymicro_wakeword import MicroWakeWord

HERE = Path(__file__).resolve().parent
OUT = HERE / "golden"
MODELS = HERE.parents[1] / "models"
MODEL_NAMES = ["hey_jarvis", "okay_nabu", "hey_mycroft", "vad"]
CLIPS = {
    "hey_jarvis": ("Daniel", "Hey Jarvis"),
    "okay_nabu": ("Daniel", "Okay Nabu"),
    "hey_mycroft": ("Daniel", "Hey Mycroft"),
    "speech_uk": ("Lesya", "Сьогодні гарна погода, тож після обіду ми підемо гуляти в парк."),
}
RATE = 16000
STEP = 160
FEATURE_SCALE = 0.0390625


def synthesize(voice: str, text: str) -> np.ndarray:
    with tempfile.TemporaryDirectory() as tmp:
        aiff = Path(tmp) / "say.aiff"
        wav = Path(tmp) / "say.wav"
        subprocess.run(["say", "-v", voice, "-o", str(aiff), text], check=True)
        subprocess.run(["afconvert", "-f", "WAVE", "-d", f"LEI16@{RATE}", "-c", "1", str(aiff), str(wav)], check=True)
        data = wav.read_bytes()
    at = data.index(b"data") + 8
    return np.frombuffer(data[at:], dtype="<i2")


def framed(speech: np.ndarray, seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    lead = RATE // 2
    tail = RATE
    total = lead + len(speech) + tail
    total += (-total) % STEP
    signal = rng.normal(0, 20, total)
    signal[lead : lead + len(speech)] += speech
    return np.clip(np.rint(signal), -32768, 32767).astype("<i2")


def features(audio: np.ndarray) -> list[list[int]]:
    frontend = MicroFrontend()
    frames = []
    raw = audio.tobytes()
    for at in range(0, len(raw) - 2 * STEP + 1, 2 * STEP):
        out = frontend.process_samples(raw[at : at + 2 * STEP])
        if out.features:
            frames.append([int(round(f / FEATURE_SCALE)) for f in out.features])
    return frames


def probabilities(frames: list[list[int]]) -> dict[str, list[float]]:
    result = {}
    for name in MODEL_NAMES:
        model = MicroWakeWord.from_config(MODELS / f"{name}.json")
        probs = []
        for i, frame in enumerate(frames):
            model.process_streaming_prob(np.array(frame, dtype=np.float64).reshape(1, 1, -1) * FEATURE_SCALE)
            if (i + 1) % model.stride == 0:
                probs.append(model._probabilities[-1])
        result[name] = probs
    return result


def main() -> None:
    OUT.mkdir(exist_ok=True)
    if "--synthesize" in sys.argv:
        for seed, (clip, (voice, text)) in enumerate(CLIPS.items(), start=1):
            (OUT / f"wake_{clip}.raw").write_bytes(framed(synthesize(voice, text), seed).tobytes())
        return
    if platform.machine().lower() not in ("x86_64", "amd64"):
        sys.exit("run on x86_64: pymicro-features builds for other machines use fused multiply-add and differ")
    for clip in CLIPS:
        audio = np.frombuffer((OUT / f"wake_{clip}.raw").read_bytes(), dtype="<i2")
        frames = features(audio)
        (OUT / f"wake_{clip}.features.txt").write_text("".join(" ".join(map(str, f)) + "\n" for f in frames))
        probs = probabilities(frames)
        lines = [" ".join(f"{probs[n][i]:.6f}" for n in MODEL_NAMES) for i in range(len(probs[MODEL_NAMES[0]]))]
        (OUT / f"wake_{clip}.probs.txt").write_text("".join(line + "\n" for line in lines))
        peak = max(max(f) for f in frames)
        means = {n: max(np.convolve(probs[n], np.ones(5) / 5, mode="valid")) for n in MODEL_NAMES}
        print(clip, f"{len(audio) / RATE:.2f} s", len(frames), "frames, peak feature", peak,
              " ".join(f"{n}={means[n]:.3f}" for n in MODEL_NAMES))


if __name__ == "__main__":
    main()
