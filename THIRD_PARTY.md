# Third-party components

Pytaichenko is built on the projects below. They keep their own licenses, separate from the licenses of this repository
([LICENSE](LICENSE) for the code, [LICENSE-hardware](LICENSE-hardware) for the case, renders and guides).

## ESPHome

[ESPHome](https://esphome.io) ([source](https://github.com/esphome/esphome)) is the build system of the firmware and
provides the code that runs on the speaker. Its Python parts are under the MIT License, its C++ parts, the code that
runs on the board, under the GPL-3.0.

The published firmware binaries (`pytaichenko-<version>.factory.bin` in GitHub Releases and the image of the web installer
on the site) therefore include GPL-3.0 code. Their source is ESPHome itself, at the version pinned in
`firmware/requirements.txt`, plus the configuration in `firmware/` at the same release tag: `pytaichenko.yaml`, `packages/`
and the sounds in `sounds/`; `firmware/build-web.sh` builds them. While building, ESPHome also downloads and compiles
the libraries it depends on, among them Espressif's ESP-IDF, each under its own license.

## micro_wake_word models

The wake phrase models Hey Jarvis, Okay Nabu and Hey Mycroft and the voice activity model (VAD) come from
[esphome/micro-wake-word-models](https://github.com/esphome/micro-wake-word-models), pinned at commit
`40ff33f57f8fc6ad71a75ef085abbf742495b225` in `firmware/packages/wake_word.yaml`, and are built into the firmware
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

The test recordings `hub/samples/*.wav` are synthetic speech made with the OpenAI text-to-speech API by
`hub/samples/make.sh`: the voice is AI-generated, not a recording of a person.

## Installed or loaded at use, not bundled

- The server's Python dependencies, listed in `hub/pyproject.toml`, are installed from PyPI when the package or the
  Docker image is built.
- The site loads [ESP Web Tools](https://github.com/esphome/esp-web-tools) (Apache License 2.0) from unpkg.com for the
  firmware install button.
