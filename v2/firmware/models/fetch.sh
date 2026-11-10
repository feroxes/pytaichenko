#!/bin/sh
set -eu

COMMIT=40ff33f57f8fc6ad71a75ef085abbf742495b225
BASE=https://raw.githubusercontent.com/esphome/micro-wake-word-models/$COMMIT
DIR=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

fetch() {
    curl -sSfL -o "$TMP/$2" "$BASE/$1"
    echo "$3  $TMP/$2" | shasum -a 256 -c - >/dev/null
    mv "$TMP/$2" "$DIR/$2"
    echo "$2 ok"
}

fetch LICENSE LICENSE c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4
fetch models/v2/hey_jarvis.json hey_jarvis.json b153867d818675d8abcc9dace474afe7f83551ae0d5a9b1d71a98681320185af
fetch models/v2/hey_jarvis.tflite hey_jarvis.tflite 21a7976add39ee24ec96c63d96b7aaa18e24d1d9824b963e451da8feb4b78b77
fetch models/v2/okay_nabu.json okay_nabu.json 6dd65604f70fe5ea9d1af73a7bf239529d1fbabc363807f45d2b22ce464ddbed
fetch models/v2/okay_nabu.tflite okay_nabu.tflite 0689abe1912a95a3318a0d8cb2e67bad0cbcfe3e24dd6e050c75debddfb6f891
fetch models/v2/hey_mycroft.json hey_mycroft.json 57b2b06fe5fdbbe834a242fabc7af31e4194a550fc382b2c88636a6d62d0d57e
fetch models/v2/hey_mycroft.tflite hey_mycroft.tflite c2a9b6ed51182db72e014781d5a4ece1929dc232a40b5b4be384f0295f0e1571
fetch models/v2/vad.json vad.json bd8c9cd4350814f761e651685ae5196e2a56521aaf5670071ba742927de58ed7
fetch models/v2/vad.tflite vad.tflite 7aa4db6d5fb7c5358609f6931e7847d303c16a43d178638bc14104f50d7eff5f
