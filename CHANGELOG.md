# Changelog

Notable changes to Pytaichenko, newest first. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). There are two speakers, each with its own line of versions: 1.x is version 1 (with a home server), 2.x is version 2 (on its own). Within a line, a new middle number means new features and a new last number fixes.

Помітні зміни «Питайченка», найновіші зверху. Колонок дві, у кожної своя лінія версій: 1.x — версія 1 (з домашнім сервером), 2.x — версія 2 (сама по собі). У межах лінії нове середнє число означає нові можливості, останнє — виправлення.

## [2.0.0] - 2026-11-10

Version 2: a new speaker that talks with OpenAI by itself. Version 1 stays as it is.

### Added

- Speaker: an ESP32-S3-DevKitC N16R8 board, two INMP441 microphones, a MAX98357A amplifier with a Visaton FRS 5 X driver, a light ring of one LED filament and a button on top. The wiring, step-by-step soldering with the wire lengths and the pin map are on the site.
- Firmware: written for ESP-IDF. The speaker hears the wake phrase itself (Hey Jarvis, Okay Nabu or Hey Mycroft), cleans the sound of its two microphones and holds the conversation with the OpenAI Realtime API using your own key. It answers in one of ten languages, keeps listening for a follow-up, ends on “thank you, that’s all” and changes the volume by voice.
- Settings page on the speaker itself, in Ukrainian and English: password, OpenAI key and connection check, conversation language and instructions, wake phrase, volume with a sound test, timings, log and updates from GitHub or from a file. Holding the top button for 10 seconds resets the page password.
- Case: a compact round case (Ø84 mm) for 3D printing, with the OpenSCAD model, ready STL files for PETG and TPU and step-by-step assembly pictures.
- Site: a version switch; the version 2 pages cover the parts, printing, soldering, assembly, installing the firmware from the browser and troubleshooting.

### Українською

Версія 2: нова колонка, яка сама розмовляє з OpenAI. Версія 1 лишається як є.

- Колонка: плата ESP32-S3-DevKitC N16R8, два мікрофони INMP441, підсилювач MAX98357A з динаміком Visaton FRS 5 X, кільце світла з однієї світлодіодної нитки й кнопка зверху. Схема підключення, покрокова пайка з довжинами дротів і карта виводів — на сайті.
- Прошивка: написана на ESP-IDF. Колонка сама чує ключову фразу (Hey Jarvis, Okay Nabu або Hey Mycroft), очищує звук двох мікрофонів і веде розмову через OpenAI Realtime API з вашим власним ключем. Відповідає однією з десяти мов, після відповіді слухає продовження, завершує розмову на «дякую, все» й міняє гучність голосом.
- Сторінка налаштувань на самій колонці, українською й англійською: пароль, ключ OpenAI і перевірка з'єднання, мова й інструкції розмови, ключова фраза, гучність із тестом звуку, часи, журнал і оновлення з GitHub або з файлу. Якщо затиснути верхню кнопку на 10 секунд, пароль сторінки скидається.
- Корпус: компактний круглий корпус (Ø84 мм) для 3D-друку — модель OpenSCAD, готові STL-файли для PETG і TPU та покрокові малюнки збирання.
- Сайт: перемикач версій; сторінки версії 2 — деталі, друк, пайка, збирання, встановлення прошивки з браузера й «Якщо щось не так».

## [1.0.0] - 2026-11-02

The first public release: everything needed to build the speaker and run it at home.

### Added

- Speaker: an ESP32 DevKit V1 board with an INMP441 microphone, a MAX98357A amplifier and a speaker, a light ring of two LED filaments and a button on top. The wiring, the soldering steps and the wire lengths are on the site.
- Case: a round case for 3D printing, with the OpenSCAD model, ready STL files for PETG and TPU and step-by-step assembly pictures.
- Firmware: the speaker hears the wake phrase by itself (Hey Jarvis, Okay Nabu or Hey Mycroft), keeps listening for a follow-up after an answer and shows what it is doing with the light ring and short sounds. It installs from the browser on the site, and later versions install the same way without erasing the speaker.
- Server: `voice-hub` runs in Docker on a computer in the home network and holds the conversation with the OpenAI Realtime API using your own key. Its settings page, in Ukrainian and English, sets the password, the OpenAI key, the conversation language and instructions and the timings, finds and adopts speakers and picks their wake phrase.
- Site: the project page in Ukrainian and English with the parts and where to buy them, tools, printing, soldering, assembly, firmware install, server setup and troubleshooting.

### Українською

Перший публічний випуск: усе, що потрібно, щоб зібрати колонку й запустити її вдома.

- Колонка: плата ESP32 DevKit V1, мікрофон INMP441, підсилювач MAX98357A і динамік, кільце світла з двох світлодіодних ниток і кнопка зверху. Схема підключення, кроки пайки й довжини дротів — на сайті.
- Корпус: круглий корпус для 3D-друку — модель OpenSCAD, готові STL-файли для PETG і TPU та покрокові малюнки збирання.
- Прошивка: колонка сама чує ключову фразу (Hey Jarvis, Okay Nabu або Hey Mycroft), після відповіді слухає продовження без фрази й показує, що робить, кільцем світла й короткими звуками. Її ставлять з браузера на сайті, а наступні версії ставляться так само, без стирання колонки.
- Сервер: `voice-hub` працює в Docker на комп'ютері в домашній мережі й веде розмову через OpenAI Realtime API з вашим власним ключем. На його сторінці налаштувань, українською й англійською, задають пароль, ключ OpenAI, мову й інструкції розмови та часи, знаходять і забирають колонки, обирають їм ключову фразу.
- Сайт: сторінка проєкту українською й англійською — деталі й де їх купити, інструменти, друк, пайка, збирання, встановлення прошивки, запуск сервера й «Якщо щось не так».

[2.0.0]: https://github.com/feroxes/pytaichenko/releases/tag/v2.0.0
[1.0.0]: https://github.com/feroxes/pytaichenko/releases/tag/v1.0.0
