# Third-party components

Pytaichenko is built on the projects below. They keep their own licenses, separate from the licenses of this repository
([LICENSE](LICENSE) for the code, [LICENSE-hardware](LICENSE-hardware) for the case, renders and guides).

The sections ESPHome, micro_wake_word models and OpenAI Realtime API describe version 1 (`v1/`). The section
Version 2 firmware describes version 2 (`v2/`).

## ESPHome

[ESPHome](https://esphome.io) ([source](https://github.com/esphome/esphome)) is the build system of the version 1
firmware and provides the code that runs on the speaker. Its Python parts are under the MIT License, its C++ parts, the
code that runs on the board, under the GPL-3.0.

The published version 1 firmware binaries (`pytaichenko-1.Y.Z.factory.bin` in GitHub Releases and the image of the web
installer on the site) therefore include GPL-3.0 code. Their source is ESPHome itself, at the version pinned in
`v1/firmware/requirements.txt`, plus the configuration in `v1/firmware/` at the same release tag: `pytaichenko.yaml`, `packages/`
and the sounds in `sounds/`; `v1/firmware/build-web.sh` builds them. While building, ESPHome also downloads and compiles
the libraries it depends on, among them Espressif's ESP-IDF, each under its own license.

## micro_wake_word models

The wake phrase models Hey Jarvis, Okay Nabu and Hey Mycroft and the voice activity model (VAD) come from
[esphome/micro-wake-word-models](https://github.com/esphome/micro-wake-word-models), pinned at commit
`40ff33f57f8fc6ad71a75ef085abbf742495b225` in `v1/firmware/packages/wake_word.yaml`, and are built into the firmware
binaries. They are licensed under the [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0),
© 2024-2026 Open Home Foundation. Their NOTICE file asks for this attribution:

> ESPHome micro_wake_word Model Collection
> Copyright 2024-2026 Open Home Foundation
>
> This product includes software and model files developed by the
> Open Home Foundation (https://www.openhomefoundation.org/).

The wake phrase names may refer to products or characters owned by others. They only name what each model detects;
no affiliation with or endorsement by their owners is implied.

## OpenAI Realtime API

The server talks to the [OpenAI](https://openai.com) Realtime API with the user's own API key. No OpenAI software is
bundled in this repository or in the releases; using the API is subject to
[OpenAI's terms and policies](https://openai.com/policies/).

The test recordings `v1/hub/samples/*.wav` are synthetic speech made with the OpenAI text-to-speech API by
`v1/hub/samples/make.sh`: the voice is AI-generated, not a recording of a person.

## Version 2 firmware

The version 2 firmware (`v2/firmware`) is built with Espressif's ESP-IDF and the components below, and the published
images (`pytaichenko-2.Y.Z.factory.bin`, `pytaichenko-2.Y.Z.ota.bin` and the parts of the web installer on the site)
include their code. The ESP-IDF component manager downloads the components from the
[ESP Component Registry](https://components.espressif.com) at the exact versions in `v2/firmware/dependencies.lock`. The
licenses below were read from each component's own files at those versions.

The speaker talks to the same OpenAI Realtime API as the version 1 server, with the user's own key, which is kept on the
speaker. What is said under OpenAI Realtime API above applies to it as well.

| Component | Version | License | Source |
|---|---|---|---|
| ESP-IDF | 5.5.6 | Apache-2.0 | [espressif/esp-idf](https://github.com/espressif/esp-idf), version in `v2/firmware/IDF_VERSION` |
| `espressif/esp-sr` | 2.6.0 | ESPRESSIF MIT License, see below | [espressif/esp-sr](https://github.com/espressif/esp-sr) |
| `espressif/esp-tflite-micro` | 1.4.1 | Apache-2.0 | [espressif/esp-tflite-micro](https://github.com/espressif/esp-tflite-micro) |
| `espressif/esp-nn` | 1.4.2 | Apache-2.0 | [espressif/esp-nn](https://github.com/espressif/esp-nn) |
| `espressif/esp-dl` | 3.3.14 | MIT | [espressif/esp-dl](https://github.com/espressif/esp-dl), pulled in by esp-sr |
| `espressif/dl_fft` | 0.8.0 | MIT, as stated in its component manifest (the package has no license file) | [espressif/esp-dl](https://github.com/espressif/esp-dl/tree/master/esp-dl/tools/dl_fft), pulled in by esp-sr and esp-dl |
| `espressif/esp_new_jpeg` | 1.0.2 | ESPRESSIF MIT License, see below | [espressif/esp-adf-libs](https://github.com/espressif/esp-adf-libs/tree/master/esp_new_jpeg), pulled in by esp-dl |
| `espressif/cjson` | 1.7.19~2 | MIT | [espressif/idf-extra-components](https://github.com/espressif/idf-extra-components/tree/master/cjson), pulled in by esp-sr |
| `espressif/esp_websocket_client` | 1.6.1 | Apache-2.0 | [espressif/esp-protocols](https://github.com/espressif/esp-protocols/tree/master/components/esp_websocket_client) |
| `espressif/mdns` | 1.14.1 | Apache-2.0 | [espressif/esp-protocols](https://github.com/espressif/esp-protocols/tree/master/components/mdns) |

### ESP-IDF and the libraries it brings

ESP-IDF is © Espressif Systems and licensed under the [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0).
It includes third-party libraries that end up in the firmware, each under its own license, as listed on its
[Copyrights and Licenses](https://github.com/espressif/esp-idf/blob/v5.5.6/docs/en/COPYRIGHT.rst) page: among them
FreeRTOS (MIT), lwIP (BSD), Newlib (BSD) and wpa_supplicant (BSD).

- **cJSON** (MIT, © 2009-2017 Dave Gamble and cJSON contributors): the firmware's own code uses the copy in ESP-IDF's
  `json` component; esp-sr pulls in `espressif/cjson` as well.
- **Mbed TLS** 3.6.7, the version inside ESP-IDF 5.5.6, is provided under Apache-2.0 OR GPL-2.0-or-later; it is used
  here under the Apache License 2.0. It protects the connections to OpenAI and to GitHub for updates.

### esp-sr

esp-sr provides the audio front end that cleans up the sound of the two microphones before the conversation, with the
WebRTC voice activity detector inside it. Part of it ships as precompiled libraries. No esp-sr model files are flashed:
the images hold only the bootloader, the partition table, the OTA data and the app.

esp-sr is licensed under the ESPRESSIF MIT License, © 2018 Espressif Systems (Shanghai) PTE LTD. In short, its terms
are those of the MIT License (use, copy, modify, merge, publish, distribute, sublicense and sell, free of charge, with
the copyright and permission notice kept in all copies or substantial portions, and no warranty), but the permission is
granted only for use on Espressif Systems products. The firmware runs only on an ESP32-S3, an Espressif chip.
`espressif/esp_new_jpeg`, which esp-dl pulls in, carries the same license, © 2024 Espressif Systems (Shanghai) CO., LTD.

### TensorFlow Lite Micro

`espressif/esp-tflite-micro` is Espressif's packaging of [TensorFlow Lite Micro](https://github.com/tensorflow/tflite-micro),
© The TensorFlow Authors, Apache License 2.0. It runs the wake phrase models, and `espressif/esp-nn` (Apache-2.0)
speeds up its operations on the ESP32-S3. It includes:

- kissfft (BSD-3-Clause, © 2003-2010 Mark Borgerding), which computes the audio features the models listen to;
- FlatBuffers and gemmlowp (Apache-2.0);
- ruy (Apache-2.0, © Google LLC).

### microWakeWord models

The version 2 firmware uses the same models as version 1, Hey Jarvis, Okay Nabu, Hey Mycroft and the voice activity
model (VAD), from [esphome/micro-wake-word-models](https://github.com/esphome/micro-wake-word-models) at commit
`40ff33f57f8fc6ad71a75ef085abbf742495b225`. `v2/firmware/models/fetch.sh` downloads them from that commit, checks their
sha256 and keeps them in `v2/firmware/models/`, and they are built into the firmware. They are licensed under the Apache
License 2.0 (`v2/firmware/models/LICENSE`), © 2024-2026 Open Home Foundation, and the NOTICE quoted under
micro_wake_word models above applies to them too.

### libsoxr, acknowledgement

The resampler of the version 2 firmware (`v2/firmware/components/pt_core/src/pt_dsp.c`) designs its low-pass filters by
following the published filter-design formulas and Kaiser window beta values of the SoX Resampler library
[libsoxr](https://sourceforge.net/projects/soxr/), © 2007-18 robs@users.sourceforge.net, LGPL-2.1 or later. No libsoxr
code was copied into this repository, so the LGPL-2.1 obligations do not apply; this is a courtesy acknowledgement of
that work.

### Tests and tools, not in the firmware

- The host tests (`v2/firmware/tests/host/CMakeLists.txt`) download, for the tests only:
  [Unity](https://github.com/ThrowTheSwitch/Unity) 2.6.1 (MIT), [cJSON](https://github.com/DaveGamble/cJSON) 1.7.18
  (MIT), [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) 3.6.7 (Apache-2.0 OR GPL-2.0-or-later) and esp-tflite-micro
  at the commit the board uses (Apache-2.0, with kissfft under BSD-3-Clause).
- The reference data in `v2/firmware/tests/host/golden/wake_*` is made with pymicro-features and pymicro-wakeword, and
  the two signal sounds are converted by `v2/firmware/tools/make_sounds.py` with
  [python-soxr](https://github.com/dofuuz/python-soxr) (LGPL-2.1 or later). These run on the developer's computer and
  are installed from PyPI; none of them is part of the firmware.

### Test voice clips

The wake phrase tests use short voice clips (`v2/firmware/tests/host/golden/wake_*`). They are not in the repository:
they are synthesized on a Mac with the macOS `say` command, whose voices may only be used personally. Run
`v2/firmware/tests/host/make_wake_golden.py --synthesize` on a Mac to make them; without them those tests are skipped.

## Installed or loaded at use, not bundled

- The server's Python dependencies, listed in `v1/hub/pyproject.toml`, are installed from PyPI when the package or the
  Docker image is built.
- The site loads [ESP Web Tools](https://github.com/esphome/esp-web-tools) 10.4.0 (Apache License 2.0) from unpkg.com
  for the firmware install button of both versions.
