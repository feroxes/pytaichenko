#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
: "${OPENAI_API_KEY:?set OPENAI_API_KEY}"

make_sample() {
  local name="$1" text="$2"
  python3 -c 'import json, sys; print(json.dumps({"model": "gpt-4o-mini-tts", "voice": "coral", "input": sys.argv[1], "instructions": "Говори українською, природно й спокійно, як людина, що звертається до голосового асистента.", "response_format": "pcm"}))' "$text" |
    curl -sS --fail https://api.openai.com/v1/audio/speech -H @<(printf 'Authorization: Bearer %s\n' "$OPENAI_API_KEY") \
      -H "Content-Type: application/json" -d @- -o "$name.pcm"
  python3 -c 'import sys, wave; w = wave.open(sys.argv[2], "wb"); w.setnchannels(1); w.setsampwidth(2); w.setframerate(24000); w.writeframes(open(sys.argv[1], "rb").read()); w.close()' "$name.pcm" "$name.tts.wav"
  afconvert -f WAVE -d LEI16@16000 -c 1 "$name.tts.wav" "$name.wav"
  rm -f "$name.pcm" "$name.tts.wav"
}

make_sample long "Це перевірка потокового відтворення. Пристрій має програти цей запис від початку до кінця, без обривів і без помилок у журналі. Раз, два, три, чотири, п'ять."
make_sample q1 "Привіт, скільки буде сім помножити на вісім?"
make_sample q2 "Розкажи коротко, чому небо блакитне."
make_sample q3 "Зроби трохи тихіше."
make_sample q4 "А якщо помножити на дев'ять?"
make_sample q5 "Дякую, все."
