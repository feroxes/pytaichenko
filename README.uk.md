# Питайченко

[English](README.md) · **Українська**

<p align="center">
  <img src="site/assets/photo.jpg" width="640" alt="Питайченко на столі: круглий чорний корпус із решіткою динаміка зверху й білою смужкою кільця під кришкою">
</p>

<p align="center"><b>Голосова колонка, яку збирають самі: вона чує ключову фразу просто на платі, а розмову з OpenAI Realtime API веде невеликий сервер у вашій домашній мережі.</b></p>

<p align="center">
  <a href="https://github.com/feroxes/pytaichenko/releases/latest"><img src="https://img.shields.io/github/v/release/feroxes/pytaichenko?label=%D0%B2%D0%B8%D0%BF%D1%83%D1%81%D0%BA" alt="Останній випуск"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/%D0%BA%D0%BE%D0%B4-MIT-blue" alt="Код: MIT"></a>
  <a href="LICENSE-hardware"><img src="https://img.shields.io/badge/%D0%BA%D0%BE%D1%80%D0%BF%D1%83%D1%81%20%D1%96%20%D1%82%D0%B5%D0%BA%D1%81%D1%82%D0%B8-CC%20BY--SA%204.0-lightgrey" alt="Корпус і тексти: CC BY-SA 4.0"></a>
</p>

<p align="center"><a href="https://feroxes.github.io/pytaichenko/?lang=uk"><b>Покрокова інструкція</b></a> · <a href="https://github.com/feroxes/pytaichenko/releases/latest">Останній випуск</a></p>

## Що вміє

- Вмикається ключовою фразою або натиском кнопки зверху. Фраза — одна з трьох: Hey Jarvis, Okay Nabu або Hey Mycroft; її обирають для кожної колонки на сторінці налаштувань.
- Відповідає голосом. Після відповіді можна питати далі без фрази.
- «Дякую, все» завершує розмову. Гучність змінюється голосом або повзунком на сторінці налаштувань.
- Кільце світла й два короткі сигнали показують, що робить колонка.
- На сторінці налаштувань сервера, українською й англійською, усе зібрано в одному місці: пароль, ключ OpenAI, модель і голос, мова розмови (українська чи англійська) та інструкції, часи й колонки.
- Прошивку ставлять з браузера через USB, а нові версії ставляться так само, без стирання колонки.

## Як це працює

<p align="center">
  <img src="site/assets/how-it-works-uk.svg" width="760" alt="Як це працює: ви питаєте колонку вголос; удома колонка сама чує ключову фразу й передає питання службі voice-hub — вашому домашньому серверу зі сторінкою налаштувань і вашим ключем OpenAI; voice-hub говорить з OpenAI Realtime, а відповідь голосом повертається тим самим шляхом">
</p>

- Колонка розпізнає ключову фразу на власній платі. Доки вона не почула фразу або ви не натиснули кнопку, жоден звук із неї нікуди не йде.
- Далі вона надсилає ваш голос службі `voice-hub` — невеликому серверу в Docker на комп'ютері у вашій домашній мережі. Служба зберігає ваш ключ OpenAI, веде розмову з OpenAI Realtime API й повертає колонці відповідь голосом.
- Після відповіді колонка ще кілька секунд слухає продовження, а потім знову чекає ключову фразу.

## Як зібрати

[Покрокова інструкція](https://feroxes.github.io/pytaichenko/?lang=uk) показує все з малюнками: деталі з посиланнями, друк, пайку й збирання. Крім деталей, знадобляться 3D-принтер, паяльник, комп'ютер удома, що працює постійно й має Docker, і ключ OpenAI API; розмови оплачуються за цінами OpenAI.

1. [Придбайте деталі](https://feroxes.github.io/pytaichenko/?lang=uk#parts) й [надрукуйте корпус](https://feroxes.github.io/pytaichenko/?lang=uk#print).
2. [Спаяйте](https://feroxes.github.io/pytaichenko/?lang=uk#solder) й [зберіть](https://feroxes.github.io/pytaichenko/?lang=uk#assembly) колонку.
3. [Встановіть прошивку](https://feroxes.github.io/pytaichenko/?lang=uk#firmware) з браузера через USB.
4. [Запустіть сервер](https://feroxes.github.io/pytaichenko/?lang=uk#server) і заберіть колонку на його сторінці налаштувань.

Якщо щось не працює, загляньте в розділ [«Якщо щось не так»](https://feroxes.github.io/pytaichenko/?lang=uk#trouble).

## Версії й випуски

- Прошивка, сервер і сайт мають одну спільну версію.
- X.0.0 — нове покоління колонки, тобто нове залізо; наступне буде 2.0.0.
- Кожен [випуск](https://github.com/feroxes/pytaichenko/releases) містить образ прошивки `pytaichenko-X.Y.Z.factory.bin` і всі STL-файли корпусу в `pytaichenko-X.Y.Z-stl.zip`.

## Ліцензія

- Код (конфігурація прошивки, сервер, код сайту, скрипти й тести) поширюється за [ліцензією MIT](LICENSE).
- Модель корпусу (`case/*.scad`), STL-файли (`case/stl`), малюнки (`site/assets/renders`) і тексти інструкцій (файли README й тексти сайту) поширюються за [CC BY-SA 4.0](LICENSE-hardware).
- Сторонні частини мають власні ліцензії: образи прошивки містять код ESPHome під GPL-3.0 і моделі ключових фраз під Apache-2.0. Див. [THIRD_PARTY.md](THIRD_PARTY.md).

## Подяки

- [ESPHome](https://esphome.io) — за систему збирання прошивки, компоненти голосу й ключових фраз та ESP Web Tools, що ставить прошивку з браузера.
- Open Home Foundation — за [моделі microWakeWord](https://github.com/esphome/micro-wake-word-models) Hey Jarvis, Okay Nabu і Hey Mycroft.
- [OpenAI API](https://platform.openai.com) — за розмову в реальному часі (Realtime API).
