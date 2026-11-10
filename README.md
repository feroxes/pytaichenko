# Pytaichenko

**English** · [Українська](README.uk.md)

<p align="center">
  <img src="site/assets/photos-v2/pytaichenko.jpg" width="640" alt="Pytaichenko on a desk: a low round black case, the top covered in small holes, a thin white ring strip under it and a small microphone hole on the side">
</p>

<p align="center"><b>A voice speaker you build yourself: it hears its wake phrase on the board and holds the conversation with the OpenAI Realtime API.</b></p>

<p align="center">
  <a href="https://github.com/feroxes/pytaichenko/releases/latest"><img src="https://img.shields.io/github/v/release/feroxes/pytaichenko" alt="Latest release"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/code-MIT-blue" alt="Code: MIT"></a>
  <a href="LICENSE-hardware"><img src="https://img.shields.io/badge/case%20and%20guides-CC%20BY--SA%204.0-lightgrey" alt="Case and guides: CC BY-SA 4.0"></a>
  <a href="https://donatello.to/feroxes"><img src="https://img.shields.io/badge/%E2%99%A5-Donate-e0475b" alt="Donate"></a>
</p>

<p align="center"><a href="https://feroxes.github.io/pytaichenko/?lang=en&v=2"><b>Step-by-step guide</b></a> · <a href="https://github.com/feroxes/pytaichenko/releases/latest">Latest release</a></p>

There are two versions of the speaker, each with its own hardware and its own pages in the guide.

**Version 2** talks with OpenAI by itself. It is built on an ESP32-S3 board, and the speaker serves its own settings page. [Guide for version 2](https://feroxes.github.io/pytaichenko/?lang=en&v=2)

**Version 1** needs a small home server: the `voice-hub` service in Docker holds the conversation and serves the settings page. It is built on an ESP32 board. [Guide for version 1](https://feroxes.github.io/pytaichenko/?lang=en&v=1)

## What it does

- Starts with a wake phrase or a press of the button on top. The phrase is one of three: Hey Jarvis, Okay Nabu or Hey Mycroft, chosen for each speaker on the settings page.
- Answers out loud. After an answer you can keep asking without the phrase.
- “Thank you, that’s all” ends the conversation. The volume changes by voice or with a slider on the settings page.
- A light ring and two short sounds show what the speaker is doing.
- The settings page, in Ukrainian and English, holds the OpenAI key, the model, the voice, the instructions, the conversation language, the timings and the wake phrase. In version 2 the speaker serves it itself. In version 1 it is the server's page, which also holds the list of speakers.
- The firmware installs from the browser over USB. Version 2 then updates from its settings page, without a cable; in version 1 new versions install from the browser the same way, without erasing the speaker.

## How it works

<p align="center">
  <img src="site/assets/how-it-works-en.svg" width="760" alt="How version 1 works: you ask the speaker out loud; at home the speaker hears the wake phrase by itself and passes your question to voice-hub, your home server with the settings page and your OpenAI key; voice-hub talks to OpenAI Realtime, and the spoken answer comes back the same way">
</p>

- In both versions the speaker recognises the wake phrase on its own board. Until it hears the phrase or you press the button, no sound leaves it.
- Version 2 then sends your voice straight to the OpenAI Realtime API. The speaker keeps your OpenAI key and plays the spoken answer itself.
- Version 1, shown in the picture, sends your voice to `voice-hub`, a small service in Docker on a computer in your home network. The service keeps your OpenAI key, holds the conversation with the OpenAI Realtime API and sends the spoken answer back to the speaker.
- After the answer the speaker listens a few more seconds for a follow-up, then waits for the wake phrase again.

## Build it

The step-by-step guide shows everything in pictures for each version: the parts with links, printing, soldering and assembly. Besides the parts you need a 3D printer, a soldering iron, 2.4 GHz Wi-Fi and an OpenAI API key; conversations are paid at OpenAI prices. Version 1 also needs a computer at home that stays on and runs Docker.

**Version 2**

1. [Get the parts](https://feroxes.github.io/pytaichenko/?lang=en&v=2#parts) and [print the case](https://feroxes.github.io/pytaichenko/?lang=en&v=2#print).
2. [Solder](https://feroxes.github.io/pytaichenko/?lang=en&v=2#solder) and [assemble](https://feroxes.github.io/pytaichenko/?lang=en&v=2#assembly) the speaker.
3. [Install the firmware](https://feroxes.github.io/pytaichenko/?lang=en&v=2#firmware) from the browser over USB, then open the speaker's settings page and enter your OpenAI key.

**Version 1**

1. [Get the parts](https://feroxes.github.io/pytaichenko/?lang=en&v=1#parts) and [print the case](https://feroxes.github.io/pytaichenko/?lang=en&v=1#print).
2. [Solder](https://feroxes.github.io/pytaichenko/?lang=en&v=1#solder) and [assemble](https://feroxes.github.io/pytaichenko/?lang=en&v=1#assembly) the speaker.
3. [Install the firmware](https://feroxes.github.io/pytaichenko/?lang=en&v=1#firmware) from the browser over USB.
4. [Start the server](https://feroxes.github.io/pytaichenko/?lang=en&v=1#server) and adopt the speaker on its settings page.

If something does not work, see If something goes wrong for [version 2](https://feroxes.github.io/pytaichenko/?lang=en&v=2#trouble) or [version 1](https://feroxes.github.io/pytaichenko/?lang=en&v=1#trouble).

## Versions and releases

- X.0.0 is a new speaker generation, that is new hardware: version 1 is released as `v1.x.y`, version 2 as `v2.x.y`, starting with 2.0.0.
- Each version has its own version number. In version 1 the firmware and the server share it; in version 2 it is the number of the firmware.
- Every [release](https://github.com/feroxes/pytaichenko/releases) of version 1 carries the firmware image `pytaichenko-1.Y.Z.factory.bin` and all the case STL files in `pytaichenko-1.Y.Z-stl.zip`.
- Every release of version 2 carries the firmware image `pytaichenko-2.Y.Z.factory.bin` for the first install over USB, the update `pytaichenko-2.Y.Z.ota.bin` that the speaker installs from its settings page, and all the case STL files in `pytaichenko-2.Y.Z-stl.zip`.
- The Latest release badge and link show the newest release of version 2; the releases of version 1 stay on the releases page. The site always offers the newest release of each version.

## License

- The code (the version 2 firmware, the version 1 firmware configuration and server, the site code, scripts and tests) is under the [MIT License](LICENSE).
- The case design (`v1/case/*.scad`, `v2/case/*.scad`), the STL files (`v1/case/stl`, `v2/case/stl`), the renders (`site/assets/renders`) and the written guides (the README files and the texts of the site) are under [CC BY-SA 4.0](LICENSE-hardware).
- Third-party parts keep their own licenses: the version 1 firmware images include ESPHome code under GPL-3.0; the version 2 images are built on Espressif's ESP-IDF and components under Apache-2.0, MIT and the Espressif MIT License; both include wake phrase models under Apache-2.0. See [THIRD_PARTY.md](THIRD_PARTY.md).

## Thanks

- [Espressif](https://www.espressif.com), for [ESP-IDF](https://github.com/espressif/esp-idf) and the components the version 2 firmware is built on, among them esp-sr, which cleans up the sound of the two microphones, and esp-tflite-micro, which runs the wake phrase models.
- [ESPHome](https://esphome.io), for the version 1 firmware build system, its voice and wake phrase components and ESP Web Tools, which installs the firmware of both versions from the browser.
- The Open Home Foundation, for the [microWakeWord models](https://github.com/esphome/micro-wake-word-models) Hey Jarvis, Okay Nabu and Hey Mycroft.
- The [OpenAI API](https://platform.openai.com), for the conversation in real time (the Realtime API).
