# Changelog

Notable changes to Pytaichenko, newest first. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). One version number covers the firmware, the server and the site: a new first number means a new speaker (new hardware), a new middle number new features for the same speaker, a new last number fixes.

Помітні зміни «Питайченка», найновіші зверху. Одна версія для прошивки, сервера й сайту: нове перше число означає нову колонку (нове залізо), середнє — нові можливості для тієї самої колонки, останнє — виправлення.

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

[1.0.0]: https://github.com/feroxes/pytaichenko/releases/tag/v1.0.0
