# Pytaichenko firmware, version 2

C firmware for the ESP32-S3-DevKitC N16R8, built with ESP-IDF. The version is in `IDF_VERSION`.

## Build and flash

Install ESP-IDF once:

    git clone -b $(cat v2/firmware/IDF_VERSION) --recursive https://github.com/espressif/esp-idf ~/esp/esp-idf
    ~/esp/esp-idf/install.sh esp32s3

Then, in every new shell:

    . ~/esp/esp-idf/export.sh
    idf.py -C v2/firmware set-target esp32s3
    idf.py -C v2/firmware build
    idf.py -C v2/firmware -p /dev/cu.usbmodemXXXX flash monitor

`dependencies.lock` pins the component versions. Regenerate it (delete it and run `idf.py -C v2/firmware reconfigure`)
and commit the result whenever `IDF_VERSION` or any `idf_component.yml` changes.

Use the board's USB port labelled UART (or COM), not the one labelled USB. That port is UART0: it carries the
log console and Improv Wi-Fi setup.

## First run on the board

Not yet done on a real board. Run this once when the ESP32-S3-DevKitC N16R8 arrives:

1. Connect the UART port with a data cable. Find the port with `ls /dev/cu.usb*`. The DevKitC's CH343 bridge shows
   up on macOS as `/dev/cu.usbmodem…`.
2. `idf.py -C v2/firmware -p <port> erase-flash flash monitor`. Expected in the log: `Pytaichenko <version>
   started, name pytaichenko-xxxxxx`, `no saved settings, using defaults`, `no Wi-Fi network set yet`.
3. Close the monitor (Ctrl-]). In Chrome open a page with the Improv serial button (a local HTML file with
   `<improv-wifi-serial-launch-button>` from `improv-wifi-serial-sdk@2.8.1`; improv-wifi.com has no button), connect to
   the port and enter the Wi-Fi name and password. Expected: the page offers `http://pytaichenko-xxxxxx.local/`; in the monitor
   `Wi-Fi connected, address …` and `new Wi-Fi settings saved`. The password must not appear in the log.
4. Try a wrong Wi-Fi password once: Improv reports "unable to connect" and the board goes back to the network it
   had before.
5. Open `http://pytaichenko-xxxxxx.local/`: the settings page loads. Create a password, log in, change and save a
   setting, move the volume slider. The Log section shows the start lines and `settings applied`.
6. Note how long the password check takes (PBKDF2 on the board) and the line `httpd stack: N bytes never used`
   in the monitor after a login and a settings save. Under about 2 KB means the stack in `pt_http.c` is too small.
7. Restart the board (`idf.py monitor`, then Ctrl-T Ctrl-R). Wi-Fi, settings and the password are kept. Logins
   live in memory, so the page asks to log in again; the old password works.
8. Turn the router off for over 60 s: the log says `no Wi-Fi connection for 60 s`. Improv still
   works over USB, so step 3 can be repeated without erasing the board.
9. Right after a reset or a fresh flash, open the Improv page from step 3 and connect. The first
   GET_CURRENT_STATE must be answered without a retry or delay (the check depends on memory test and init order).
   If it is not answered, that is a plan 2 fix.
10. Fill the log (restart, then trigger enough events to reach the 400-line limit, for example by saving settings
   repeatedly). Note the free internal heap before and after opening the Log tab with a full log; the page
   fetch builds one buffer of up to about 170 KB, so the drop must leave a safe margin.
11. Settings page, Conversation tab, with a real OpenAI key saved: press "Check the connection to OpenAI". Expected: after
    a few seconds "The connection works. Model …, voice …", and the log line `OpenAI connection check: ok`. Then save a
    wrong key and check again: "OpenAI did not accept the key" (HTTP 401 from the handshake). Then turn the router off
    and check again: "could not reach OpenAI". While the check runs the page is slow to answer (one HTTP task). Note
    `httpd stack: N bytes never used` after a check: the TLS write happens on the HTTP task, so under about 2 KB means
    the stack in `pt_http.c` needs raising. The key must not appear in the log.

## Version and updates

The firmware version is one line in `VERSION` (now `2.0.0`). `CMakeLists.txt` reads it into `PROJECT_VER`, so it
ends up in the image (`esp_app_desc_t.version`), and `pt_core` is compiled with the same text, so `pt_version()` (the
page, the log) cannot differ from the image. The host tests read the same file. The first release is `2.0.0`: change
that one line to `2.0.0` and tag `v2.0.0`; later releases go the same way (`2.1.0`, tag `v2.1.0`).

A release has three assets:

- `pytaichenko-2.Y.Z.factory.bin`: the merged image (bootloader, partition table, OTA data and the app) to write at
  offset 0, for flashing by hand with esptool.
- `pytaichenko-2.Y.Z.ota.bin`: `build/pytaichenko.bin`, for updates from the settings page.
- `pytaichenko-2.Y.Z-stl.zip`: the case STL files.

The site does not flash the merged image. Its ESP Web Tools manifest lists four parts: `bootloader.bin` at 0x0,
`partition-table.bin` at 0x8000, `ota_data_initial.bin` at 0x19000 and `pytaichenko.bin` at 0x20000. The site's
"Update" keeps Wi-Fi, the password and the OpenAI key because no part touches the settings area (`nvs`, 0x9000 to
0x18FFF); the settings are erased only when "Erase device" is ticked.

To build the web image locally, after `idf.py -C v2/firmware build`:

    bash v2/firmware/build-web.sh

It reads the offsets and flash settings from `build/flasher_args.json`, merges the parts with `esptool.py merge_bin`
into `build/dist/pytaichenko.factory.bin`, copies the app to `build/dist/pytaichenko.ota.bin`, and writes the parts and
`manifest.json` (name `Pytaichenko.Speaker`, the name the firmware reports over Improv) to `site/firmware/v2`
(git-ignored). The version is `VERSION` without its `-dev` suffix. It refuses any part that overlaps an `nvs`
partition of `partitions.csv`. Options: `--out DIR`, `--dist DIR`, `--version X.Y.Z`, `--manifest-only` (no build
needed), and `--from-factory FILE --version X.Y.Z`, which cuts the four parts out of a released merged image using
the partition table inside that image, so an older release splits by its own layout (for publishing the site from a
release without a build). It refuses a part over that table's `nvs` partition and any data between the OTA data and
the app. Before writing, the output folder loses its old `.bin` files and `manifest.json`. Tests:
`python -m pytest -q v2/firmware/tests/test_build_web.py` (needs pytest only).

The settings page, tab "About the speaker", block "Update", has three buttons:

- "Check for updates" asks GitHub (`GET https://api.github.com/repos/feroxes/pytaichenko/releases?per_page=10`) only
  when pressed. Unauthenticated requests are limited to 60 an hour per address, so nothing checks in a loop.
  The answer (about 11 KB per release, so up to about 115 KB for ten) is read into a PSRAM buffer capped at 160 KB and
  scanned in place by `pt_ota_pick`, which does not build a JSON tree and allocates nothing.
- "Update to X" downloads the asset and installs it.
- "Upload the file" installs a `.ota.bin` picked on the computer, for when the speaker cannot reach GitHub or for a
  home build. The page sends it as one `POST /api/update/upload` with `Content-Type: application/octet-stream` (up to
  4 MB). The speaker reads the body in 4 KB pieces straight into flash and never holds the whole file in memory. Login
  and the same-site checks apply as for every other route.

Release contract for the release workflow (`.github/workflows/release.yml`): a release is taken as an update when its tag is `v2.Y.Z` (no `-`
suffix), it is neither a draft nor a pre-release, and it has an asset named exactly `pytaichenko-<Y.Z version>.ota.bin`,
for example tag `v2.1.0` and asset `pytaichenko-2.1.0.ota.bin`. That asset is the `build/pytaichenko.bin` of the
release build. The download address must start with `https://github.com/feroxes/pytaichenko/releases/download/`.

Before anything is written, the first 288 bytes of the image are checked: the project name must be `pytaichenko` and
the version must start with `2.`; otherwise the speaker answers `update_not_ours` and the flash is untouched. Updates
are refused with `update_busy` while a conversation is running; a kept OpenAI connection left over from a finished
conversation is closed before the download starts. During an update the speaker ignores the wake word and
the button (it keeps listening to the microphone, but nothing starts a conversation), and restarts 2.5 seconds after
the new image has been written and checked.

Rollback is on (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, no eFuse is used). A new image starts as "pending". If Wi-Fi is
connected and the page server has started, it is marked as working 30 seconds after start (if Wi-Fi is not up yet, the
check repeats every 10 seconds). If the new image crashes or restarts before that, the bootloader goes back to the
previous one. Turning rollback on changes the bootloader, so a board flashed before this change needs one USB flash
(`erase-flash` is not needed) before updates can be tried; after that, updates need no cable.

Checks on the board for this feature:

1. Flash by USB. In the log, after "settings page at ...", the line "this firmware is new" appears only after an update,
   not after a USB flash.
2. About tab: "Check for updates" with no release published answers "There are no new versions yet." (or the
   GitHub status in the Log).
3. Upload a `pytaichenko.bin` built with a higher `VERSION` through "Upload the file": progress, "restarting", the page
   reloads, the version has changed. In the Log: "firmware upload: N bytes written", then after the restart "the new
   firmware works, it is kept" about 30 seconds later.
4. Upload a file of another project (or any text file): "This file does not fit"; the speaker keeps running and does
   not restart.
5. Rollback: build with a deliberate crash on start, upload it, and see the speaker come back on the old version
   after the crash loop.

## Host tests

The logic in `components/pt_core` is plain C and is tested on the computer. No ESP-IDF is needed.

    cmake -S v2/firmware/tests/host -B v2/firmware/build-host -G Ninja
    cmake --build v2/firmware/build-host
    ctest --test-dir v2/firmware/build-host --output-on-failure

The first run downloads Unity, cJSON and mbedTLS. Tests run with the address and undefined behaviour
sanitizers. If the address sanitizer does not run on your machine, only the undefined behaviour one is used.
Add `-DPT_REQUIRE_SANITIZERS=ON` to make that an error.

To add a test, create `tests/host/test_<name>.c` and add `pt_add_test(<name>)` to `tests/host/CMakeLists.txt`.
Each test is built with every file in `components/pt_core/src`.

The wake word tests (`test_features`, `test_wake`) also build `components/pt_wake` with TensorFlow Lite Micro and
kissfft from the same esp-tflite-micro commit the board uses. Their reference data in `tests/host/golden/wake_*` comes
from `pymicro-features` 2.0.2 and `pymicro-wakeword` 2.5.0. The clips are not in the repository (the macOS
voices may only be used personally): synthesize them on macOS with
`python tests/host/make_wake_golden.py --synthesize` (voices Daniel and Lesya); without them these tests are skipped. The features and probabilities must
be made on x86_64, because the builds for other machines use fused multiply-add and differ slightly:

    docker run --rm --platform linux/amd64 -v "$PWD/v2/firmware:/w/v2/firmware" python:3.12 sh -c \
      'pip install numpy==2.5.4 pymicro-features==2.0.2 pymicro-wakeword==2.5.0 &&
       python -I /w/v2/firmware/tests/host/make_wake_golden.py'

## Wake word models

`models/` holds the microWakeWord models (Hey Jarvis, Okay Nabu, Hey Mycroft and the voice activity model) from
esphome/micro-wake-word-models, Apache License 2.0 (`models/LICENSE`). `models/fetch.sh` downloads them again from the
pinned commit and checks their sha256. They are built into the firmware.
