(function (window) {
  'use strict';

  var H = (window.PYTAICHENKO_HUB = window.PYTAICHENKO_HUB || {});
  var doc = window.document;
  var LANGS = ['uk', 'en'];
  var THEMES = ['dark', 'light'];
  var LANG_KEY = 'pytaichenko-lang';
  var THEME_KEY = 'pytaichenko-theme';
  var TABS = ['speakers', 'conversation', 'timing', 'access'];
  var POLL_MS = 5000;
  var REQUEST_MS = 70000;
  var TAKE_MS = 120000;
  var MIN_PASSWORD = 8;
  var MIN_STOP_GRACE = 0.5;
  var MIN_SERVER_PORT = 1024;
  var HOST_LABEL = /^[A-Za-z0-9_-]{1,63}$/;
  var HOST_ZONE = /^[A-Za-z0-9_.-]+$/;

  var DEFAULTS = {
    openai: { vad_threshold: 0.5, silence_ms: 600 },
    timing: {
      first_turn_s: 8,
      follow_up_s: 10,
      max_speech_s: 20,
      response_timeout_s: 30,
      answer_max_s: 180,
      session_idle_s: 300,
      mic_stop_grace_s: 0.5,
      mic_holdoff_s: 0.4,
      ready_check_s: 2,
      max_follow_ups: 20
    },
    http_port: 8080,
    port: 6053
  };

  var TIMING_FIELDS = [
    { name: 'first_turn_s', kind: 'seconds', group: 'main' },
    { name: 'follow_up_s', kind: 'seconds', group: 'main' },
    { name: 'max_follow_ups', kind: 'count', group: 'main' },
    { name: 'max_speech_s', kind: 'seconds', group: 'main' },
    { name: 'answer_max_s', kind: 'seconds', group: 'main' },
    { name: 'session_idle_s', kind: 'seconds', group: 'main' },
    { name: 'response_timeout_s', kind: 'seconds', group: 'more' },
    { name: 'mic_holdoff_s', kind: 'seconds0', group: 'more' },
    { name: 'mic_stop_grace_s', kind: 'grace', group: 'more' },
    { name: 'ready_check_s', kind: 'seconds', group: 'more' }
  ];

  var SPECS = {
    openai: [
      { name: 'api_key', kind: 'secret' },
      { name: 'model', kind: 'choice' },
      { name: 'voice', kind: 'choice' },
      { name: 'reasoning_effort', kind: 'choice' },
      { name: 'log_heard', kind: 'bool' },
      { name: 'vad_threshold', kind: 'unit' },
      { name: 'silence_ms', kind: 'ms' }
    ],
    assistant: [
      { name: 'language', kind: 'choice' },
      { name: 'instructions', kind: 'longtext' }
    ],
    timing: TIMING_FIELDS.map(function (f) {
      return { name: f.name, kind: f.kind };
    }),
    server: [
      { name: 'http_port', kind: 'server_port' },
      { name: 'advertise_host', kind: 'host' }
    ]
  };

  var FIELD_LABELS = {
    'openai.api_key': 'openai.key',
    'openai.model': 'openai.model',
    'openai.voice': 'openai.voice',
    'openai.reasoning_effort': 'openai.reasoning',
    'openai.vad_threshold': 'openai.vad',
    'openai.silence_ms': 'openai.silence',
    'openai.log_heard': 'field.log_heard',
    'assistant.language': 'assistant.language',
    'assistant.instructions': 'assistant.instructions',
    http_port: 'server.port',
    advertise_host: 'server.advertise',
    host: 'manual.host',
    port: 'manual.port',
    key: 'manual.key',
    password: 'login.password',
    current: 'password.current',
    new: 'password.new',
    repeat: 'password.repeat',
    wake_word: 'speaker.wake',
    volume: 'speaker.volume'
  };

  var CONTEXT_CODES = {
    password: { wrong_password: 'wrong_current' },
    volume: { offline: 'volume_offline', failed: 'volume_failed' }
  };
  var DETAILED = { save_failed: true };

  var RULES = [
    [/^must be a non-empty string( or null)?$/, 'rule.empty'],
    [/^must be valid UTF-8 text$/, 'rule.text'],
    [/^must be a whole number from (\d+) to (\d+)$/, 'rule.int_range', ['low', 'high']],
    [/^must be a whole number of at least (\d+)$/, 'rule.int_min', ['low']],
    [/^must be a number from 0 to 1$/, 'rule.unit'],
    [/^must be true or false$/, 'rule.bool'],
    [/^must be one of (.+)$/, 'rule.one_of', ['list']],
    [/^must be 0 or more seconds$/, 'rule.zero_or_more'],
    [/^must be 0\.5 or more seconds$/, 'rule.grace'],
    [/^must be a number of seconds above 0$/, 'rule.above_zero'],
    [/^must be base64 of \d+ bytes( or null)?$/, 'rule.key'],
    [/^unknown (setting|field)$/, 'rule.unknown'],
    [/^required$/, 'rule.required'],
    [/^expected (an object|a list)$/, 'rule.format'],
    [/^must be text of at most (\d+) characters$/, 'rule.text_max', ['n']],
    [/^must be (\d+) to (\d+) characters$/, 'rule.length', ['low', 'high']],
    [/^another speaker is already called/, 'rule.taken'],
    [/^must be a host name or an IP address, without http:\/\/, a port or a path$/, 'rule.host']
  ];

  var KIND_RULES = {
    seconds: { key: 'rule.above_zero' },
    seconds0: { key: 'rule.zero_or_more' },
    grace: { key: 'rule.grace' },
    count: { key: 'rule.int_min', vars: { low: 0 } },
    ms: { key: 'rule.int_min', vars: { low: 1 } },
    unit: { key: 'rule.unit' },
    port: { key: 'rule.int_range', vars: { low: 1, high: 65535 } },
    server_port: { key: 'rule.int_range', vars: { low: MIN_SERVER_PORT, high: 65535 } },
    host: { key: 'rule.host' }
  };

  var TEXT = {
    uk: {
      'meta.title': 'Питайченко — налаштування',
      'header.name': 'Питайченко',
      'header.sub': 'налаштування',
      'header.theme_label': 'Світла тема',
      'header.lang_label': 'Мова сторінки',
      'header.logout': 'Вийти',
      loading: 'Завантаження…',
      retry: 'Спробувати ще раз',
      cancel: 'Скасувати',
      save: 'Зберегти',
      saving: 'Зберігаю…',
      saved: 'Збережено. Діє з наступного питання.',
      saved_plain: 'Збережено.',
      nothing: 'Нічого не змінено.',
      more: 'Тонкі налаштування',
      optional: "необов'язково",
      'form.check': 'Перевірте позначене поле.',
      'form.unsaved': 'Є незбережені зміни',
      'unit.s': 'с',
      'unit.ms': 'мс',
      'unit.seconds': '{n} с',
      'unit.millis': '{n} мс',
      'unit.minutes': '{n} хв',
      'unit.hours': '{n} год',
      'lang.uk': 'українська',
      'lang.en': 'англійська',
      'offline.title': 'Сервер не відповідає',
      'offline.text': "Сторінка не може зв'язатися із сервером. Перевірте, що сервер запущений, і спробуйте ще раз.",
      'host.title': 'Сервер не приймає цю адресу',
      'host.opened': 'Сторінку відкрито за адресою {host}.',
      'host.why': "Щоб ніхто чужий не дістався налаштувань через підставне ім'я з інтернету, сервер відкриває їх лише за домашньою адресою:",
      'host.ip': 'IP-адреса, наприклад http://192.0.2.10:8080',
      'host.label': "ім'я без крапок, наприклад http://server:8080",
      'host.suffix': "ім'я, що закінчується на .local, .lan, .home, .internal або .home.arpa",
      'host.fix': "Якщо ви заходите на сервер за іншим ім'ям (наприклад, через власний домен), додайте це ім'я до змінної VOICE_HUB_HOSTS під час запуску сервера і перезапустіть його. Кілька імен пишуть через кому:",
      'setup.title': 'Створіть пароль',
      'setup.lead': 'Пароль захищає налаштування: ключ OpenAI, колонки й інструкції. Його знатимете лише ви.',
      'setup.language': 'Асистент розмовлятиме мовою цієї сторінки — українською. Іншу мову можна обрати потім у розділі «Розмова».',
      'setup.password': 'Пароль',
      'setup.repeat': 'Повторіть пароль',
      'setup.submit': 'Створити пароль',
      'login.title': 'Вхід',
      'login.password': 'Пароль',
      'login.submit': 'Увійти',
      'login.checking': 'Перевіряю…',
      'login.empty': 'Введіть пароль.',
      'login.env': 'Пароль задано на сервері змінною VOICE_HUB_PASSWORD.',
      'login.expired': 'Вхід закінчився (наприклад, сервер перезапустили). Увійдіть знову.',
      'login.loggedout': 'Ви вийшли. Колонки працюють і далі.',
      'password.rule': 'Щонайменше 8 символів.',
      'password.forgot': 'Якщо колись забудете пароль: запустіть сервер зі змінною VOICE_HUB_PASSWORD — тоді вхід буде за паролем із неї.',
      'password.title': 'Пароль сторінки',
      'password.env': 'Пароль задано на сервері змінною VOICE_HUB_PASSWORD. Щоб змінити його, змініть змінну й перезапустіть сервер.',
      'password.current': 'Поточний пароль',
      'password.new': 'Новий пароль',
      'password.repeat': 'Повторіть новий пароль',
      'password.hint': 'Щонайменше 8 символів. Після зміни вхід на інших пристроях закриється.',
      'password.submit': 'Змінити пароль',
      'password.changed': 'Пароль змінено. На інших пристроях доведеться увійти знову.',
      'password.mismatch': 'Паролі не збігаються.',
      'password.short': 'Закороткий пароль: потрібно щонайменше 8 символів.',
      'password.empty': 'Введіть поточний пароль.',
      'nav.label': 'Розділи',
      'nav.speakers': 'Колонки',
      'nav.conversation': 'Розмова',
      'nav.timing': 'Часи',
      'nav.access': 'Доступ',
      'banner.restart': 'Новий порт або адреса сервера запрацюють після перезапуску сервера.',
      'banner.nokey': 'Ключ OpenAI ще не задано, тож колонки не зможуть відповідати.',
      'banner.nokey_link': 'Вписати ключ',
      'banner.offline': "Немає зв'язку із сервером. Сторінка пробує знову сама.",
      'banner.settings': 'Не вдалося завантажити налаштування із сервера. Сторінка пробує знову сама.',
      'settings.not_loaded': 'Налаштування ще не завантажились із сервера, тож зберегти нічого не можна. Зачекайте трохи й спробуйте ще раз.',
      'speakers.title': 'Колонки',
      'speakers.mine': 'Ваші колонки',
      'speakers.empty': "Колонок ще немає. Увімкніть щойно прошиту колонку — за хвилину вона з'явиться нижче, у розділі «Знайдено в мережі».",
      'speaker.online': "на зв'язку",
      'speaker.offline': "не на зв'язку",
      'speaker.nokey': 'без ключа',
      'speaker.offline_hint': 'Перевірте, чи колонка ввімкнена. Якщо роутер видав їй нову адресу, змініть адресу.',
      'speaker.nokey_hint': 'Сервер не має ключа цієї колонки. Приберіть її зі списку й додайте знову: заберіть серед знайдених у мережі або додайте вручну з її ключем.',
      'speaker.wake': 'Ключова фраза',
      'speaker.wake_offline': "Фразу можна обрати, коли колонка на зв'язку.",
      'speaker.wake_none': 'Колонка не повідомила, які фрази знає.',
      'speaker.wake_saving': 'Змінюю фразу…',
      'speaker.wake_saved': 'Тепер колонка слухає «{phrase}».',
      'speaker.volume': 'Гучність',
      'speaker.volume_value': '{n}\u00a0%',
      'speaker.volume_hint': 'Голосом теж можна: «тихіше», «гучніше».',
      'speaker.move': 'Змінити адресу',
      'speaker.new_host': 'Нова адреса',
      'speaker.move_hint': 'Якщо роутер видав колонці іншу адресу. Ключ лишається той самий.',
      'speaker.move_save': 'Зберегти адресу',
      'speaker.moved': 'Адресу змінено. Колонка підключається за новою адресою.',
      'speaker.remove': 'Прибрати',
      'speaker.remove_ok': 'Прибрати',
      'speaker.remove_confirm': 'Прибрати колонку «{name}»? Сервер забуде її ключ. Щоб додати її знову, колонку доведеться перепрошити зі сторінки проєкту зі стиранням (erase).',
      'speaker.remove_confirm_nokey': 'Прибрати колонку «{name}» зі списку? Її ключа сервер не має, тож нічого не втратиться. Потім заберіть її серед знайдених у мережі або додайте вручну з її ключем.',
      'speaker.removed': 'Колонку «{name}» прибрано.',
      'found.title': 'Знайдено в мережі',
      'found.lead': "Нові колонки, які чекають на ключ, з'являються тут самі, поки сторінка відкрита.",
      'found.empty': "Нових колонок поки немає. Щойно прошита колонка з'являється тут приблизно за хвилину після ввімкнення. Якщо її немає, додайте її вручну за адресою з роутера.",
      'found.adopt': 'Забрати',
      'found.waiting': 'чекає на ключ',
      'found.others': 'Інші пристрої ESPHome у мережі: {n}',
      'found.others_hint': 'Ці пристрої вже мають власний ключ. Якщо серед них є ваша колонка, додайте її вручну з її ключем.',
      'found.reflashed': "Ця колонка вже додана, але зараз не на зв'язку. Якщо її перепрошили зі стиранням, заберіть її знову.",
      'found.adopt_again': 'Забрати знову',
      'found.add_manual': 'Додати з ключем',
      'adopt.progress': 'Колонка {host} отримує ключ і підключається. Це може тривати до хвилини.',
      'adopt.checking': 'Перевіряю колонку {host} з ключем. Це може тривати до хвилини.',
      'adopt.elapsed': 'Минуло {s} с.',
      'adopt.leave': 'Можна не чекати: сервер завершить і без цієї сторінки.',
      'adopt.done': 'Колонку «{name}» додано. Тепер можна обрати її ключову фразу.',
      'adopt.slow': 'Колонка довго не відповідає. Оновіть сторінку за хвилину — можливо, її вже додано.',
      'manual.title': 'Додати вручну',
      'manual.lead': 'Якщо колонки немає серед знайдених. Її адресу можна подивитися в налаштуваннях роутера.',
      'manual.host': 'Адреса колонки',
      'manual.port': 'Порт',
      'manual.key': 'Ключ колонки',
      'manual.key_hint': 'Лише якщо колонку прошито з власним ключем. Для колонки, щойно прошитої зі сторінки проєкту, залиште поле порожнім: сервер сам видасть їй ключ.',
      'manual.submit': 'Додати',
      'manual.filled': 'Адресу підставлено. Впишіть ключ колонки й натисніть «Додати».',
      'conversation.title': 'Розмова',
      'conversation.applies': 'Зміни діють з наступного питання. Якщо розмова вже йде, асистент почне її наново й не пам\'ятатиме попередніх питань.',
      'openai.title': 'OpenAI',
      'openai.key': 'Ключ OpenAI',
      'openai.key_set': 'задано ✓',
      'openai.key_unset': 'не задано',
      'openai.key_placeholder': 'sk-…',
      'openai.key_placeholder_set': 'вставте новий ключ, щоб замінити',
      'openai.key_hint': 'Ключ створюють на сайті platform.openai.com. Розмови оплачуються за тарифами OpenAI. Ключ зберігається лише на цьому сервері й назад не показується.',
      'openai.key_clear': 'Прибрати ключ',
      'openai.key_clear_ok': 'Прибрати ключ',
      'openai.key_clear_confirm': 'Прибрати ключ OpenAI із сервера? Колонки не зможуть відповідати, доки ви не впишете новий.',
      'openai.key_cleared': 'Ключ прибрано.',
      'openai.model': 'Модель',
      'openai.model_hint': 'gpt-realtime-2.1-mini — швидша й дешевша; gpt-realtime-2.1 — точніша, але дорожча.',
      'openai.voice': 'Голос',
      'openai.voice_hint': 'Голос, яким відповідає асистент. Найприродніше звучать marin і cedar.',
      'openai.reasoning': 'Обмірковування відповіді',
      'openai.reasoning_none': 'не вказувати',
      'openai.reasoning_hint': 'minimal — найшвидші відповіді. Що вище, то довше асистент думає перед відповіддю.',
      'openai.log_heard': 'Записувати в журнал сервера, що почув асистент',
      'openai.log_heard_hint': "Почутий текст з'являтиметься в журналі сервера. Це допомагає зрозуміти, чому асистент відповів не те. Трохи збільшує витрати на OpenAI.",
      'openai.vad': 'Чутливість до голосу',
      'openai.vad_hint': 'Число від 0 до 1: наскільки чітким має бути звук, щоб вважатися мовою. Більше число — менше спрацювань від шуму, але говорити треба виразніше. Типово: {v}.',
      'openai.silence': 'Тиша, що завершує питання',
      'openai.silence_hint': 'Скільки мілісекунд тиші означає, що ви договорили. Типово: {v}.',
      'field.log_heard': 'Журнал почутого',
      'field.message': '«{field}»: {rule}.',
      'assistant.title': 'Асистент',
      'assistant.language': 'Мова розмови',
      'assistant.language_hint': 'Цією мовою асистент слухає й відповідає. Спершу це мова сторінки, якою створювали пароль; перемикач мови сторінки її не змінює.',
      'assistant.language_note': 'Якщо інструкції типові, після збереження вони стануть типовими для нової мови. Власний текст лишиться як є.',
      'assistant.instructions': 'Інструкції',
      'assistant.instructions_hint': "Як асистентові поводитися й відповідати. Тут можна, наприклад, дати йому ім'я.",
      'assistant.reset': 'Повернути типові',
      'assistant.reset_ok': 'Повернути типові',
      'assistant.reset_confirm': 'Замінити інструкції типовим текстом ({lang})? Ваш власний текст буде втрачено.',
      'assistant.reset_done': 'Повернуто типові інструкції ({lang}).',
      'assistant.language_switched': 'Збережено. Мову розмови змінено, інструкції тепер типові для нової мови.',
      'assistant.language_kept': 'Збережено. Мову розмови змінено, а ваші інструкції лишились як були. Якщо вони іншою мовою, змініть їх або поверніть типові.',
      'timing.title': 'Часи',
      'timing.lead': 'Скільки колонка чекає й слухає. Частки секунди можна писати з комою, наприклад 0,4.',
      'timing.defaults': 'Типові значення',
      'timing.defaults_done': 'Підставлено типові значення. Натисніть «Зберегти», щоб застосувати.',
      'timing.default': 'Типово: {v}.',
      'timing.first_turn_s.label': 'Чекати на питання після фрази',
      'timing.first_turn_s.hint': 'Якщо за цей час після ключової фрази чи кнопки ви не почнете говорити, колонка перестане слухати.',
      'timing.follow_up_s.label': 'Чекати на наступне питання',
      'timing.follow_up_s.hint': 'Після відповіді колонка ще трохи слухає, щоб можна було питати далі без ключової фрази.',
      'timing.max_follow_ups.label': 'Питань поспіль без фрази',
      'timing.max_follow_ups.hint': 'Після стількох питань поспіль колонка перестає слухати, доки знову не скажете фразу. 0 — фраза потрібна щоразу.',
      'timing.max_speech_s.label': 'Найдовше питання',
      'timing.max_speech_s.hint': 'Якщо говорити довше, асистент почне відповідати на те, що вже почув.',
      'timing.answer_max_s.label': 'Найдовша відповідь',
      'timing.answer_max_s.hint': 'Довша відповідь обривається.',
      'timing.session_idle_s.label': "Пам'ятати розмову",
      'timing.session_idle_s.hint': "Стільки часу після останньої відповіді асистент пам'ятає, про що йшлося. Потім розмова починається заново, і змінені налаштування набувають сили.",
      'timing.response_timeout_s.label': 'Чекати на відповідь OpenAI',
      'timing.response_timeout_s.hint': 'Якщо OpenAI мовчить довше, колонка повідомить про помилку.',
      'timing.mic_holdoff_s.label': 'Пропустити початок звуку з мікрофона',
      'timing.mic_holdoff_s.hint': 'Вмикаючись, мікрофон клацає — цей шматочок не надсилається. 0 — нічого не пропускати.',
      'timing.mic_stop_grace_s.label': 'Пауза перед відповіддю',
      'timing.mic_stop_grace_s.hint': 'Коротка пауза після вашого питання, щоб колонка встигла вимкнути мікрофон, перш ніж заграє відповідь. Щонайменше 0,5 с.',
      'timing.ready_check_s.label': "Перевірка з'єднання з OpenAI",
      'timing.ready_check_s.hint': "Перед новим питанням сервер перевіряє, чи ще живе відкрите з'єднання, і чекає на відгук стільки секунд. Не дочекавшись, відкриває нове.",
      'access.title': 'Доступ',
      'server.title': 'Адреса сервера',
      'server.lead': 'Зазвичай тут нічого міняти не треба. Зміни запрацюють після перезапуску сервера.',
      'server.port': 'Порт сторінки й звуку',
      'server.port_hint': 'Типово: {v}. Після перезапуску сторінку треба буде відкривати з новим портом.',
      'server.port_env': 'Порт задано на сервері змінною VOICE_HUB_PORT. Щоб змінити його, змініть змінну й перезапустіть сервер.',
      'server.advertise': 'Адреса сервера для колонок',
      'server.advertise_hint': 'Колонки забирають звук відповіді за цією адресою. Залиште порожнім, і сервер визначить її сам. Вкажіть, якщо сервер працює в Docker без host-мережі.',
      'server.restart': 'Збережено. Запрацює після перезапуску сервера.',
      'logout.title': 'Вийти',
      'logout.text': 'Вихід лише на цьому пристрої. Колонки працюватимуть і далі.',
      'rule.empty': 'значення не може бути порожнім',
      'rule.text': 'текст містить символи, які не можна зберегти',
      'rule.int_range': 'потрібне ціле число від {low} до {high}',
      'rule.int_min': 'потрібне ціле число, не менше за {low}',
      'rule.unit': 'потрібне число від 0 до 1',
      'rule.bool': 'потрібно ввімкнути або вимкнути',
      'rule.one_of': 'потрібне одне зі значень: {list}',
      'rule.zero_or_more': 'потрібне число секунд, 0 або більше',
      'rule.grace': 'потрібне число секунд, 0,5 або більше',
      'rule.above_zero': 'потрібне число секунд, більше за 0',
      'rule.key': 'потрібен ключ base64 на 32 байти (44 символи, як у прошивці)',
      'rule.unknown': 'сервер такого не знає; мабуть, сторінка старіша за сервер, тож оновіть її',
      'rule.required': "значення обов'язкове",
      'rule.format': 'неправильний формат',
      'rule.text_max': 'потрібен текст до {n} символів',
      'rule.length': 'потрібно від {low} до {high} символів',
      'rule.taken': "таке ім'я вже має інша колонка",
      'rule.host': "потрібна IP-адреса або ім'я в мережі, без http://, порту чи шляху",
      'rule.other': 'сервер не прийняв значення',
      'err.network': "Немає зв'язку із сервером. Перевірте, що він запущений, і спробуйте ще раз.",
      'err.timeout': 'Сервер довго не відповідає. Спробуйте ще раз трохи згодом.',
      'err.foreign_host': "Сервер не приймає адресу, за якою відкрито сторінку. Відкрийте її за IP-адресою сервера або додайте ім'я до VOICE_HUB_HOSTS.",
      'err.foreign_origin': 'Сервер відхилив запит, бо той прийшов не з цієї сторінки. Відкрийте налаштування напряму за адресою сервера.',
      'err.login_required': 'Вхід закінчився. Увійдіть знову, будь ласка.',
      'err.json_required': 'Сервер не зрозумів запит сторінки. Оновіть сторінку; якщо не допоможе, сторінка й сервер різних версій.',
      'err.bad_json': 'Сервер не зрозумів запит сторінки. Оновіть сторінку; якщо не допоможе, сторінка й сервер різних версій.',
      'err.method_not_allowed': 'Сервер не зрозумів запит сторінки. Оновіть сторінку; якщо не допоможе, сторінка й сервер різних версій.',
      'err.not_found': 'Сервер не знає такого запиту. Оновіть сторінку; якщо не допоможе, сторінка й сервер різних версій.',
      'err.too_large': 'Задовгий текст: сервер стільки не приймає. Скоротіть його.',
      'err.internal': 'На сервері щось пішло не так. Подробиці — в журналі сервера.',
      'err.error': 'Сервер відповів помилкою. Спробуйте ще раз трохи згодом.',
      'err.http': 'Сервер відповів незнайомою помилкою. Спробуйте оновити сторінку.',
      'err.save_failed': 'Сервер не зміг записати налаштування у файл. Перевірте, чи є місце на диску і чи можна писати в теку з даними.',
      'err.password_set': 'Пароль уже створено. Увійдіть із ним.',
      'err.no_password': 'Пароль ще не створено. Спершу створіть його.',
      'err.bad_password': 'Пароль має містити від 8 до 1024 символів.',
      'err.wrong_password': 'Неправильний пароль. Спробуйте ще раз.',
      'err.wrong_current': 'Поточний пароль неправильний. Спробуйте ще раз.',
      'err.password_from_env': 'Пароль задано змінною VOICE_HUB_PASSWORD на сервері, тож тут його змінити не можна.',
      'err.port_from_env': 'Порт задано змінною VOICE_HUB_PORT на сервері, тож тут його змінити не можна.',
      'err.slow_down': 'Забагато перевірок пароля з цієї адреси одночасно. Зачекайте кілька секунд і спробуйте знову.',
      'err.invalid': 'Сервер не прийняв одне зі значень. Перевірте поля форми.',
      'err.has_key': 'Ця колонка вже має ключ, тож забрати її не можна. Якщо ключ у вас є, додайте її вручну з ключем. Якщо немає, перепрошийте колонку зі сторінки проєкту зі стиранням (erase).',
      'err.unsupported': 'Прошивка цієї колонки не вміє приймати ключ. Прошийте її прошивкою зі сторінки проєкту.',
      'err.no_key_yet': 'Ця колонка ще не має ключа. Залиште поле ключа порожнім — сервер сам видасть їй ключ.',
      'err.name_taken': "У списку вже є колонка з таким ім'ям — за іншою адресою або на зв'язку. Якщо це та сама колонка з новою адресою, змініть адресу в її картці.",
      'err.wrong_key': 'Колонка не прийняла цей ключ. Перевірте, що це ключ саме з її прошивки.',
      'err.unreachable': "Не вдалося з'єднатися з колонкою. Перевірте, що вона ввімкнена, а адреса й порт правильні.",
      'err.no_name': "Колонка не назвала свого імені. Спробуйте ще раз або перепрошийте її.",
      'err.refused': 'Колонка відмовилась прийняти новий ключ. Вимкніть і ввімкніть її та спробуйте ще раз.',
      'err.unknown_speaker': 'Такої колонки вже немає в списку. Список оновлено.',
      'err.offline': "Колонка зараз не на зв'язку. Фразу можна змінити, коли вона підключиться.",
      'err.unknown': 'Колонка не знає такої фрази. Список фраз оновлено.',
      'err.failed': 'Колонка не підтвердила нову фразу. Спробуйте ще раз.',
      'err.volume_offline': "Колонка зараз не на зв'язку. Гучність можна змінити, коли вона підключиться.",
      'err.volume_failed': 'Колонка не прийняла нову гучність. Спробуйте ще раз.'
    },
    en: {
      'meta.title': 'Pytaichenko — settings',
      'header.name': 'Pytaichenko',
      'header.sub': 'settings',
      'header.theme_label': 'Light theme',
      'header.lang_label': 'Page language',
      'header.logout': 'Log out',
      loading: 'Loading…',
      retry: 'Try again',
      cancel: 'Cancel',
      save: 'Save',
      saving: 'Saving…',
      saved: 'Saved. Applies from the next question.',
      saved_plain: 'Saved.',
      nothing: 'Nothing has changed.',
      more: 'Fine-tuning',
      optional: 'optional',
      'form.check': 'Please check the marked field.',
      'form.unsaved': 'Unsaved changes',
      'unit.s': 's',
      'unit.ms': 'ms',
      'unit.seconds': '{n} s',
      'unit.millis': '{n} ms',
      'unit.minutes': '{n} min',
      'unit.hours': '{n} h',
      'lang.uk': 'Ukrainian',
      'lang.en': 'English',
      'offline.title': 'The server is not answering',
      'offline.text': 'The page cannot reach the server. Check that the server is running and try again.',
      'host.title': 'The server does not accept this address',
      'host.opened': 'This page was opened at {host}.',
      'host.why': 'So that nobody can reach the settings through a fake name from the internet, the server opens them only at a home address:',
      'host.ip': 'an IP address, for example http://192.0.2.10:8080',
      'host.label': 'a name without dots, for example http://server:8080',
      'host.suffix': 'a name ending in .local, .lan, .home, .internal or .home.arpa',
      'host.fix': 'If you reach the server by another name (for example, your own domain), add that name to the VOICE_HUB_HOSTS variable when you start the server, then restart it. Separate several names with commas:',
      'setup.title': 'Create a password',
      'setup.lead': 'The password protects the settings: the OpenAI key, the speakers and the instructions. Only you will know it.',
      'setup.language': 'The assistant will talk in the language of this page, English. You can choose another one later in the “Conversation” section.',
      'setup.password': 'Password',
      'setup.repeat': 'Repeat the password',
      'setup.submit': 'Create password',
      'login.title': 'Log in',
      'login.password': 'Password',
      'login.submit': 'Log in',
      'login.checking': 'Checking…',
      'login.empty': 'Enter the password.',
      'login.env': 'The password is set on the server with the VOICE_HUB_PASSWORD variable.',
      'login.expired': 'You have been logged out (for example, the server restarted). Please log in again.',
      'login.loggedout': 'You have logged out. The speakers keep working.',
      'password.rule': 'At least 8 characters.',
      'password.forgot': 'If you ever forget the password, start the server with the VOICE_HUB_PASSWORD variable; then you log in with the password from it.',
      'password.title': 'Page password',
      'password.env': 'The password is set on the server with the VOICE_HUB_PASSWORD variable. To change it, change the variable and restart the server.',
      'password.current': 'Current password',
      'password.new': 'New password',
      'password.repeat': 'Repeat the new password',
      'password.hint': 'At least 8 characters. After the change, other devices are logged out.',
      'password.submit': 'Change password',
      'password.changed': 'Password changed. Other devices will need to log in again.',
      'password.mismatch': 'The passwords do not match.',
      'password.short': 'Too short: use at least 8 characters.',
      'password.empty': 'Enter the current password.',
      'nav.label': 'Sections',
      'nav.speakers': 'Speakers',
      'nav.conversation': 'Conversation',
      'nav.timing': 'Timing',
      'nav.access': 'Access',
      'banner.restart': 'The new port or server address will work after the server restarts.',
      'banner.nokey': 'The OpenAI key is not set yet, so the speakers cannot answer.',
      'banner.nokey_link': 'Enter the key',
      'banner.offline': 'No connection to the server. The page keeps trying by itself.',
      'banner.settings': 'Could not load the settings from the server. The page keeps trying by itself.',
      'settings.not_loaded': 'The settings have not loaded from the server yet, so nothing can be saved. Wait a moment and try again.',
      'speakers.title': 'Speakers',
      'speakers.mine': 'Your speakers',
      'speakers.empty': 'No speakers yet. Turn on a freshly flashed speaker; within a minute it shows up below, under “Found on the network”.',
      'speaker.online': 'online',
      'speaker.offline': 'offline',
      'speaker.nokey': 'no key',
      'speaker.offline_hint': 'Check that the speaker is on. If the router gave it a new address, change the address.',
      'speaker.nokey_hint': 'The server has no key for this speaker. Remove it from the list and add it again: adopt it among the found ones or add it by hand with its key.',
      'speaker.wake': 'Wake word',
      'speaker.wake_offline': 'You can choose the wake word while the speaker is online.',
      'speaker.wake_none': 'The speaker did not report which wake words it knows.',
      'speaker.wake_saving': 'Changing the wake word…',
      'speaker.wake_saved': 'The speaker now listens for “{phrase}”.',
      'speaker.volume': 'Volume',
      'speaker.volume_value': '{n}\u00a0%',
      'speaker.volume_hint': 'You can also say “quieter” or “louder”.',
      'speaker.move': 'Change address',
      'speaker.new_host': 'New address',
      'speaker.move_hint': 'If the router gave the speaker another address. The key stays the same.',
      'speaker.move_save': 'Save address',
      'speaker.moved': 'Address changed. The speaker is connecting at the new address.',
      'speaker.remove': 'Remove',
      'speaker.remove_ok': 'Remove',
      'speaker.remove_confirm': 'Remove speaker “{name}”? The server will forget its key. To add it again, the speaker must be re-flashed from the project page with erase.',
      'speaker.remove_confirm_nokey': 'Remove speaker “{name}” from the list? The server has no key for it, so nothing is lost. Then adopt it among the found ones or add it by hand with its key.',
      'speaker.removed': 'Speaker “{name}” removed.',
      'found.title': 'Found on the network',
      'found.lead': 'New speakers waiting for a key show up here by themselves while this page is open.',
      'found.empty': 'No new speakers yet. A freshly flashed speaker shows up here about a minute after it is turned on. If it does not, add it by hand using the address from your router.',
      'found.adopt': 'Adopt',
      'found.waiting': 'waiting for a key',
      'found.others': 'Other ESPHome devices on the network: {n}',
      'found.others_hint': 'These devices already have their own key. If one of them is your speaker, add it by hand with its key.',
      'found.reflashed': 'This speaker is already added, but it is offline now. If it was re-flashed with erase, adopt it again.',
      'found.adopt_again': 'Adopt again',
      'found.add_manual': 'Add with key',
      'adopt.progress': 'The speaker at {host} is getting its key and connecting. This can take up to a minute.',
      'adopt.checking': 'Checking the speaker at {host} with the key. This can take up to a minute.',
      'adopt.elapsed': '{s} s so far.',
      'adopt.leave': 'No need to wait: the server finishes even without this page.',
      'adopt.done': 'Speaker “{name}” added. You can now choose its wake word.',
      'adopt.slow': 'The speaker is taking a long time. Reload the page in a minute; it may already be added.',
      'manual.title': 'Add by hand',
      'manual.lead': 'If the speaker is not among the found ones. You can look up its address in your router settings.',
      'manual.host': 'Speaker address',
      'manual.port': 'Port',
      'manual.key': 'Speaker key',
      'manual.key_hint': 'Only if the speaker was flashed with its own key. For a speaker freshly flashed from the project page, leave it empty: the server gives it a key itself.',
      'manual.submit': 'Add',
      'manual.filled': 'The address is filled in. Enter the speaker key and press “Add”.',
      'conversation.title': 'Conversation',
      'conversation.applies': 'Changes apply from the next question. A conversation already going on starts afresh and forgets the earlier questions.',
      'openai.title': 'OpenAI',
      'openai.key': 'OpenAI key',
      'openai.key_set': 'set ✓',
      'openai.key_unset': 'not set',
      'openai.key_placeholder': 'sk-…',
      'openai.key_placeholder_set': 'paste a new key to replace it',
      'openai.key_hint': 'You create a key at platform.openai.com. Conversations are paid at OpenAI prices. The key is kept only on this server and is never shown back.',
      'openai.key_clear': 'Remove the key',
      'openai.key_clear_ok': 'Remove the key',
      'openai.key_clear_confirm': 'Remove the OpenAI key from the server? The speakers cannot answer until you enter a new one.',
      'openai.key_cleared': 'The key has been removed.',
      'openai.model': 'Model',
      'openai.model_hint': 'gpt-realtime-2.1-mini is faster and cheaper; gpt-realtime-2.1 is more accurate but costs more.',
      'openai.voice': 'Voice',
      'openai.voice_hint': 'The voice the assistant answers with. marin and cedar sound the most natural.',
      'openai.reasoning': 'Thinking before answering',
      'openai.reasoning_none': 'do not set',
      'openai.reasoning_hint': 'minimal gives the fastest answers. The higher it is, the longer the assistant thinks before answering.',
      'openai.log_heard': 'Write what the assistant heard to the server log',
      'openai.log_heard_hint': 'The heard text shows up in the server log. It helps to understand why the assistant answered something else. Costs a little more at OpenAI.',
      'openai.vad': 'Voice sensitivity',
      'openai.vad_hint': 'A number from 0 to 1: how clear a sound must be to count as speech. A higher number means fewer false starts from noise, but you need to speak more clearly. Default: {v}.',
      'openai.silence': 'Silence that ends a question',
      'openai.silence_hint': 'How many milliseconds of silence mean you have finished speaking. Default: {v}.',
      'field.log_heard': 'Log of what was heard',
      'field.message': '“{field}” {rule}.',
      'assistant.title': 'Assistant',
      'assistant.language': 'Conversation language',
      'assistant.language_hint': 'The assistant listens and answers in this language. At first it is the page language the password was created in; switching the page language does not change it.',
      'assistant.language_note': 'If the instructions are the defaults, after saving they become the defaults for the new language. Your own text stays as it is.',
      'assistant.instructions': 'Instructions',
      'assistant.instructions_hint': 'How the assistant should behave and answer. You can, for example, give it a name here.',
      'assistant.reset': 'Restore defaults',
      'assistant.reset_ok': 'Restore defaults',
      'assistant.reset_confirm': 'Replace the instructions with the default text ({lang})? Your own text will be lost.',
      'assistant.reset_done': 'Default instructions restored ({lang}).',
      'assistant.language_switched': 'Saved. The conversation language changed and the instructions are now the defaults for it.',
      'assistant.language_kept': 'Saved. The conversation language changed and your instructions stayed as they were. If they are in another language, edit them or restore the defaults.',
      'timing.title': 'Timing',
      'timing.lead': 'How long the speaker waits and listens. Fractions of a second are written with a point, for example 0.4.',
      'timing.defaults': 'Default values',
      'timing.defaults_done': 'Default values filled in. Press “Save” to apply them.',
      'timing.default': 'Default: {v}.',
      'timing.first_turn_s.label': 'Wait for a question after the wake word',
      'timing.first_turn_s.hint': 'If you do not start speaking this long after the wake word or the button, the speaker stops listening.',
      'timing.follow_up_s.label': 'Wait for the next question',
      'timing.follow_up_s.hint': 'After an answer the speaker keeps listening a little, so you can ask more without the wake word.',
      'timing.max_follow_ups.label': 'Questions in a row without the wake word',
      'timing.max_follow_ups.hint': 'After this many questions in a row the speaker stops listening until you say the wake word again. 0 means the wake word every time.',
      'timing.max_speech_s.label': 'Longest question',
      'timing.max_speech_s.hint': 'If you speak longer, the assistant starts answering what it has heard so far.',
      'timing.answer_max_s.label': 'Longest answer',
      'timing.answer_max_s.hint': 'A longer answer is cut off.',
      'timing.session_idle_s.label': 'Remember the conversation',
      'timing.session_idle_s.hint': 'For this long after the last answer the assistant remembers what you talked about. After that a new conversation starts fresh and changed settings take effect.',
      'timing.response_timeout_s.label': 'Wait for OpenAI to answer',
      'timing.response_timeout_s.hint': 'If OpenAI stays silent longer, the speaker reports an error.',
      'timing.mic_holdoff_s.label': 'Skip the start of the microphone sound',
      'timing.mic_holdoff_s.hint': 'The microphone clicks as it turns on; this bit is not sent. 0 skips nothing.',
      'timing.mic_stop_grace_s.label': 'Pause before the answer',
      'timing.mic_stop_grace_s.hint': 'A short pause after your question, so the speaker can turn the microphone off before the answer plays. At least 0.5 s.',
      'timing.ready_check_s.label': 'OpenAI connection check',
      'timing.ready_check_s.hint': 'Before a new question the server checks that the open connection is still alive and waits this long for a reply. Without one it opens a new connection.',
      'access.title': 'Access',
      'server.title': 'Server address',
      'server.lead': 'Usually nothing needs changing here. Changes take effect after the server restarts.',
      'server.port': 'Port for the page and the sound',
      'server.port_hint': 'Default: {v}. After the restart, open the page with the new port.',
      'server.port_env': 'The port is set on the server with the VOICE_HUB_PORT variable. To change it, change the variable and restart the server.',
      'server.advertise': 'Server address for the speakers',
      'server.advertise_hint': 'The speakers fetch the answer sound from this address. Leave it empty and the server finds it itself. Set it if the server runs in Docker without host networking.',
      'server.restart': 'Saved. Takes effect after the server restarts.',
      'logout.title': 'Log out',
      'logout.text': 'Logs out on this device only. The speakers keep working.',
      'rule.empty': 'cannot be empty',
      'rule.text': 'contains characters that cannot be saved',
      'rule.int_range': 'must be a whole number from {low} to {high}',
      'rule.int_min': 'must be a whole number, {low} or more',
      'rule.unit': 'must be a number from 0 to 1',
      'rule.bool': 'must be on or off',
      'rule.one_of': 'must be one of: {list}',
      'rule.zero_or_more': 'must be a number of seconds, 0 or more',
      'rule.grace': 'must be a number of seconds, 0.5 or more',
      'rule.above_zero': 'must be a number of seconds above 0',
      'rule.key': 'must be a base64 key of 32 bytes (44 characters, as in the firmware)',
      'rule.unknown': 'is unknown to the server; the page may be older than the server, so reload it',
      'rule.required': 'is required',
      'rule.format': 'has the wrong format',
      'rule.text_max': 'must be text of at most {n} characters',
      'rule.length': 'must be {low} to {high} characters long',
      'rule.taken': 'is already used by another speaker',
      'rule.host': 'must be a host name or an IP address, without http://, a port or a path',
      'rule.other': 'was not accepted by the server',
      'err.network': 'Cannot reach the server. Check that it is running and try again.',
      'err.timeout': 'The server is taking too long to answer. Try again a bit later.',
      'err.foreign_host': 'The server does not accept the address this page was opened at. Open it by the server IP address or add the name to VOICE_HUB_HOSTS.',
      'err.foreign_origin': 'The server refused the request because it did not come from this page. Open the settings directly at the server address.',
      'err.login_required': 'You have been logged out. Please log in again.',
      'err.json_required': 'The server did not understand the page request. Reload the page; if that does not help, the page and the server are different versions.',
      'err.bad_json': 'The server did not understand the page request. Reload the page; if that does not help, the page and the server are different versions.',
      'err.method_not_allowed': 'The server did not understand the page request. Reload the page; if that does not help, the page and the server are different versions.',
      'err.not_found': 'The server does not know this request. Reload the page; if that does not help, the page and the server are different versions.',
      'err.too_large': 'The text is too long for the server. Please shorten it.',
      'err.internal': 'Something went wrong on the server. The server log has the details.',
      'err.error': 'The server answered with an error. Try again a bit later.',
      'err.http': 'The server answered with an unfamiliar error. Try reloading the page.',
      'err.save_failed': 'The server could not write the settings file. Check that there is free disk space and that the data folder is writable.',
      'err.password_set': 'A password already exists. Log in with it.',
      'err.no_password': 'No password has been created yet. Create one first.',
      'err.bad_password': 'The password must be 8 to 1024 characters long.',
      'err.wrong_password': 'Wrong password. Please try again.',
      'err.wrong_current': 'The current password is wrong. Please try again.',
      'err.password_from_env': 'The password is set by the VOICE_HUB_PASSWORD variable on the server, so it cannot be changed here.',
      'err.port_from_env': 'The port is set by the VOICE_HUB_PORT variable on the server, so it cannot be changed here.',
      'err.slow_down': 'Too many password checks from this address at once. Wait a few seconds and try again.',
      'err.invalid': 'The server did not accept one of the values. Check the form fields.',
      'err.has_key': 'This speaker already has a key, so it cannot be adopted. If you have the key, add the speaker by hand with it. If not, re-flash the speaker from the project page with erase.',
      'err.unsupported': "This speaker's firmware cannot take a key. Flash it with the firmware from the project page.",
      'err.no_key_yet': 'This speaker has no key yet. Leave the key field empty and the server will give it one.',
      'err.name_taken': 'A speaker with this name is already in the list, at another address or online. If it is the same speaker at a new address, change the address on its card.',
      'err.wrong_key': "The speaker did not accept this key. Check that it is the key from this speaker's firmware.",
      'err.unreachable': 'Could not connect to the speaker. Check that it is on and that the address and port are right.',
      'err.no_name': 'The speaker did not tell its name. Try again or re-flash it.',
      'err.refused': 'The speaker refused the new key. Turn it off and on again, then try again.',
      'err.unknown_speaker': 'This speaker is no longer in the list. The list has been refreshed.',
      'err.offline': 'The speaker is offline right now. You can change the wake word once it connects.',
      'err.unknown': 'The speaker does not know this wake word. The list has been refreshed.',
      'err.failed': 'The speaker did not confirm the new wake word. Please try again.',
      'err.volume_offline': 'The speaker is offline right now. You can change the volume once it connects.',
      'err.volume_failed': 'The speaker did not accept the new volume. Please try again.'
    }
  };

  function has(object, key) {
    return Object.prototype.hasOwnProperty.call(object, key);
  }

  function t(lang, key, vars) {
    var dict = TEXT[lang] || TEXT.uk;
    var text = has(dict, key) ? dict[key] : has(TEXT.uk, key) ? TEXT.uk[key] : key;
    if (!vars) return text;
    return text.replace(/\{(\w+)\}/g, function (all, name) {
      return has(vars, name) ? String(vars[name]) : all;
    });
  }

  function pickLang(saved, navLangs) {
    if (LANGS.indexOf(saved) >= 0) return saved;
    var list = navLangs && navLangs.length ? Array.prototype.slice.call(navLangs) : [];
    for (var i = 0; i < list.length; i++) {
      var primary = String(list[i]).toLowerCase().split('-')[0];
      if (primary === 'uk' || primary === 'ru') return 'uk';
    }
    return 'en';
  }

  function langFromSearch(search) {
    var found = /[?&]lang=([^&#]*)/.exec(String(search || ''));
    return found && LANGS.indexOf(found[1]) >= 0 ? found[1] : null;
  }

  function urlWithLang(href, lang) {
    var cut = href.indexOf('#');
    var hash = cut >= 0 ? href.slice(cut) : '';
    var rest = cut >= 0 ? href.slice(0, cut) : href;
    var q = rest.indexOf('?');
    var base = q >= 0 ? rest.slice(0, q) : rest;
    var params = (q >= 0 ? rest.slice(q + 1).split('&') : []).filter(function (part) {
      return part && part.split('=')[0] !== 'lang';
    });
    params.push('lang=' + lang);
    return base + '?' + params.join('&') + hash;
  }

  function parseNumber(text) {
    var s = String(text === null || text === undefined ? '' : text).trim().replace(',', '.');
    if (!/^[+-]?(\d+(\.\d*)?|\.\d+)$/.test(s)) return NaN;
    return Number(s);
  }

  function formatNumber(value, lang) {
    if (typeof value !== 'number' || !isFinite(value)) return '';
    var s = String(value);
    return lang === 'uk' ? s.replace('.', ',') : s;
  }

  function formatDuration(seconds, lang) {
    if (seconds >= 3600 && seconds % 3600 === 0) return t(lang, 'unit.hours', { n: seconds / 3600 });
    if (seconds >= 60 && seconds % 60 === 0) return t(lang, 'unit.minutes', { n: seconds / 60 });
    return t(lang, 'unit.seconds', { n: formatNumber(seconds, lang) });
  }

  function formatSetting(kind, value, lang) {
    if (kind === 'seconds' || kind === 'seconds0' || kind === 'grace') {
      return t(lang, 'unit.seconds', { n: formatNumber(value, lang) });
    }
    return formatNumber(value, lang);
  }

  function checkValue(kind, text) {
    var s = String(text === null || text === undefined ? '' : text).trim();
    var n = parseNumber(s);
    var whole = /^\d+$/.test(s);
    var ok;
    if (kind === 'seconds') ok = n > 0;
    else if (kind === 'seconds0') ok = n >= 0;
    else if (kind === 'grace') ok = n >= MIN_STOP_GRACE;
    else if (kind === 'count') ok = whole && n >= 0;
    else if (kind === 'ms') ok = whole && n >= 1;
    else if (kind === 'unit') ok = n >= 0 && n <= 1;
    else if (kind === 'port') ok = whole && n >= 1 && n <= 65535;
    else if (kind === 'server_port') ok = whole && n >= MIN_SERVER_PORT && n <= 65535;
    else return { rule: { key: 'rule.other' } };
    return ok ? { value: n } : { rule: KIND_RULES[kind] };
  }

  function isIPv4(text) {
    var parts = text.split('.');
    return parts.length === 4 && parts.every(function (part) {
      return /^(0|[1-9]\d{0,2})$/.test(part) && Number(part) <= 255;
    });
  }

  function isHextet(part) {
    return /^[0-9A-Fa-f]{1,4}$/.test(part);
  }

  function isIPv6(text, withoutZone) {
    if (text.indexOf('/') >= 0) return false;
    var cut = text.indexOf('%');
    if (cut >= 0) {
      if (withoutZone || !HOST_ZONE.test(text.slice(cut + 1))) return false;
      text = text.slice(0, cut);
    }
    var parts = text.split(':');
    if (parts.length < 3) return false;
    if (parts[parts.length - 1].indexOf('.') >= 0) {
      if (!isIPv4(parts[parts.length - 1])) return false;
      parts.splice(parts.length - 1, 1, '0', '0');
    }
    if (parts.length > 9) return false;
    var skip = -1;
    for (var i = 1; i < parts.length - 1; i++) {
      if (parts[i]) continue;
      if (skip >= 0) return false;
      skip = i;
    }
    if (skip < 0) return parts.length === 8 && parts.every(isHextet);
    var high = parts.slice(0, skip);
    var low = parts.slice(skip + 1);
    if (!parts[0]) {
      if (high.length !== 1) return false;
      high = [];
    }
    if (!parts[parts.length - 1]) {
      if (low.length !== 1) return false;
      low = [];
    }
    return high.length + low.length <= 7 && high.concat(low).every(isHextet);
  }

  function isHost(text, withoutZone) {
    var s = String(text === null || text === undefined ? '' : text).trim();
    if (isIPv4(s) || isIPv6(s, withoutZone)) return true;
    var name = s.charAt(s.length - 1) === '.' ? s.slice(0, -1) : s;
    var labels = name.split('.');
    return name.length <= 253 && labels.every(function (label) {
      return HOST_LABEL.test(label);
    }) && !/^\d+$/.test(labels[labels.length - 1]);
  }

  function readValue(kind, raw) {
    var s = String(raw === null || raw === undefined ? '' : raw);
    if (kind === 'secret') return s.trim() ? { value: s.trim() } : { skip: true };
    if (kind === 'text') return s.trim() ? { value: s.trim() } : { rule: { key: 'rule.empty' } };
    if (kind === 'longtext') return s.trim() ? { value: s } : { rule: { key: 'rule.empty' } };
    if (kind === 'host') {
      if (!s.trim()) return { value: null };
      return isHost(s, true) ? { value: s.trim() } : { rule: KIND_RULES.host };
    }
    if (kind === 'choice') return { value: s === '' ? null : s };
    if (kind === 'bool') return { value: raw === true };
    return checkValue(kind, s);
  }

  function buildPatch(specs, loaded, values) {
    var patch = {};
    var errors = [];
    specs.forEach(function (spec) {
      if (!has(values, spec.name)) return;
      var got = readValue(spec.kind, values[spec.name]);
      if (got.rule) {
        errors.push({ name: spec.name, rule: got.rule });
        return;
      }
      if (got.skip) return;
      if (spec.kind !== 'secret' && loaded && got.value === loaded[spec.name]) return;
      patch[spec.name] = got.value;
    });
    return { patch: patch, errors: errors, empty: Object.keys(patch).length === 0 };
  }

  function changedValues(specs, loaded, values) {
    var changed = {};
    specs.forEach(function (spec) {
      if (spec.kind === 'secret' || !has(values, spec.name)) return;
      var got = readValue(spec.kind, values[spec.name]);
      if (!got.rule && loaded && got.value === loaded[spec.name]) return;
      changed[spec.name] = values[spec.name];
    });
    return changed;
  }

  function onlineNames(devices) {
    var online = {};
    (devices || []).forEach(function (device) {
      if (device.connected && device.key_set) online[device.name] = true;
    });
    return online;
  }

  function sortFound(found, devices) {
    var online = onlineNames(devices);
    var waiting = [];
    var others = [];
    (found || []).forEach(function (item) {
      var adopt = !!item.provisionable && !has(online, item.name);
      var entry = { found: item, adopt: adopt, reflashed: adopt && !!item.added };
      if (adopt) waiting.push(entry);
      else if (!item.added) others.push(entry);
    });
    function byName(a, b) {
      return String(a.found.name).localeCompare(String(b.found.name));
    }
    return { waiting: waiting.sort(byName), others: others.sort(byName) };
  }

  function volumeOf(device) {
    var level = device && device.connected ? device.volume : null;
    return typeof level === 'number' && isFinite(level) ? level : null;
  }

  function speakerLocks(device, card, busy) {
    var words = (device && device.wake_words) || {};
    var connected = !!(device && device.connected);
    var acting = !!busy || !!card.removing || !!card.moving;
    return {
      wake: !connected || !(words.available || []).length || !!card.saving || acting,
      move: acting,
      remove: acting,
      volume: volumeOf(device) === null || acting
    };
  }

  function removeQuestion(device) {
    return device && device.key_set === false ? 'speaker.remove_confirm_nokey' : 'speaker.remove_confirm';
  }

  function ruleText(rule, lang) {
    for (var i = 0; i < RULES.length; i++) {
      var match = RULES[i][0].exec(rule);
      if (!match) continue;
      var vars = {};
      (RULES[i][2] || []).forEach(function (name, j) {
        vars[name] = match[j + 1];
      });
      return { text: t(lang, RULES[i][1], vars), known: true };
    }
    return { text: t(lang, 'rule.other'), known: false };
  }

  function fieldLabel(path, lang) {
    var first = String(path).split(', ')[0];
    var timing = /^timing\.(\w+)$/.exec(first);
    if (timing && TIMING_FIELDS.some(function (f) { return f.name === timing[1]; })) {
      return t(lang, 'timing.' + timing[1] + '.label');
    }
    return has(FIELD_LABELS, first) ? t(lang, FIELD_LABELS[first]) : null;
  }

  function sentence(text) {
    var s = String(text).trim();
    if (!s) return s;
    s = s.charAt(0).toUpperCase() + s.slice(1);
    return /[.!?…]$/.test(s) ? s : s + '.';
  }

  function splitMessage(message) {
    var s = String(message || '');
    var cut = s.indexOf(': ');
    if (cut < 1) return null;
    return { path: s.slice(0, cut), rule: s.slice(cut + 2) };
  }

  function explain(failure, lang, context) {
    var code = failure.code || 'http';
    var swap = context && CONTEXT_CODES[context];
    if (swap && has(swap, code)) code = swap[code];
    var parsed = splitMessage(failure.message);
    var dict = TEXT[lang] || TEXT.uk;
    if (code === 'invalid' && parsed) {
      var label = fieldLabel(parsed.path, lang);
      var rule = ruleText(parsed.rule, lang);
      if (label) {
        return {
          text: t(lang, 'field.message', { field: label, rule: rule.text }),
          short: sentence(rule.text),
          field: parsed.path.split(', ')[0],
          detail: rule.known ? '' : failure.message
        };
      }
      return { text: t(lang, 'err.invalid'), detail: failure.message };
    }
    if (code === 'bad_password' && parsed) {
      return { text: t(lang, 'err.bad_password'), short: t(lang, 'err.bad_password'), field: parsed.path };
    }
    if (has(dict, 'err.' + code)) {
      return { text: t(lang, 'err.' + code), detail: DETAILED[code] ? failure.message || '' : '' };
    }
    var detail = [failure.status ? String(failure.status) : '', failure.message || code].join(' ').trim();
    return { text: t(lang, 'err.http'), detail: detail };
  }

  var ERROR_CODES = Object.keys(TEXT.uk).filter(function (key) {
    return key.indexOf('err.') === 0;
  }).map(function (key) {
    return key.slice(4);
  });

  H.TEXT = TEXT;
  H.LANGS = LANGS;
  H.DEFAULTS = DEFAULTS;
  H.TIMING_FIELDS = TIMING_FIELDS;
  H.MIN_STOP_GRACE = MIN_STOP_GRACE;
  H.SPECS = SPECS;
  H.ERROR_CODES = ERROR_CODES;
  H.t = t;
  H.pickLang = pickLang;
  H.langFromSearch = langFromSearch;
  H.urlWithLang = urlWithLang;
  H.parseNumber = parseNumber;
  H.formatNumber = formatNumber;
  H.formatDuration = formatDuration;
  H.formatSetting = formatSetting;
  H.checkValue = checkValue;
  H.isHost = isHost;
  H.buildPatch = buildPatch;
  H.changedValues = changedValues;
  H.sortFound = sortFound;
  H.speakerLocks = speakerLocks;
  H.removeQuestion = removeQuestion;
  H.ruleText = ruleText;
  H.fieldLabel = fieldLabel;
  H.explain = explain;

  function readStored(key) {
    try {
      return window.localStorage.getItem(key);
    } catch (e) {
      return null;
    }
  }

  function writeStored(key, value) {
    try {
      window.localStorage.setItem(key, value);
    } catch (e) {}
  }

  function navigatorLangs() {
    var nav = window.navigator || {};
    return nav.languages && nav.languages.length ? nav.languages : nav.language ? [nav.language] : [];
  }

  var state = {
    lang: pickLang(langFromSearch((window.location || {}).search) || readStored(LANG_KEY), navigatorLangs()),
    theme: THEMES.indexOf(readStored(THEME_KEY)) >= 0 ? readStored(THEME_KEY) : 'dark',
    screen: 'loading',
    tab: 'speakers',
    auth: null,
    settings: null,
    restartNeeded: false,
    devices: [],
    found: [],
    foundShown: null,
    busy: null,
    busyTimer: null,
    loadingDevices: false,
    loadingFound: false,
    loadingSettings: false,
    drafts: null,
    session: 0,
    ids: 0
  };

  doc.documentElement.setAttribute('data-theme', state.theme);
  doc.documentElement.setAttribute('lang', state.lang);

  var cards = new Map();

  function $(selector, root) {
    return (root || doc).querySelector(selector);
  }

  function $$(selector, root) {
    return Array.prototype.slice.call((root || doc).querySelectorAll(selector));
  }

  function tr(key, vars) {
    return t(state.lang, key, vars);
  }

  function nextId(prefix) {
    state.ids += 1;
    return prefix + '-' + state.ids;
  }

  function el(tag, attrs, children) {
    var node = doc.createElement(tag);
    Object.keys(attrs || {}).forEach(function (name) {
      var value = attrs[name];
      if (value === null || value === undefined || value === false) return;
      if (name === 'text') node.textContent = value;
      else if (name === 'i18n') {
        node.setAttribute('data-i18n', value);
        node.textContent = tr(value);
      } else node.setAttribute(name, value === true ? '' : String(value));
    });
    (children || []).forEach(function (child) {
      if (child) node.appendChild(child);
    });
    return node;
  }

  function applyText(root) {
    $$('[data-i18n]', root).forEach(function (node) {
      node.textContent = tr(node.getAttribute('data-i18n'));
    });
    $$('[data-i18n-attr]', root).forEach(function (node) {
      node.getAttribute('data-i18n-attr').split(';').forEach(function (pair) {
        var cut = pair.indexOf(':');
        if (cut > 0) node.setAttribute(pair.slice(0, cut).trim(), tr(pair.slice(cut + 1).trim()));
      });
    });
  }

  function say(node, tone, describe) {
    if (!node) return;
    node._say = describe ? { tone: tone, describe: describe } : null;
    node.setAttribute('data-say', '');
    renderSaid(node);
  }

  function renderSaid(node) {
    var said = node._say;
    while (node.firstChild) node.removeChild(node.firstChild);
    if (!said) {
      node.removeAttribute('data-tone');
      return;
    }
    var out = said.describe(state.lang);
    var text = typeof out === 'string' ? out : out.text;
    var detail = typeof out === 'string' ? '' : out.detail;
    if (said.tone) node.setAttribute('data-tone', said.tone);
    else node.removeAttribute('data-tone');
    node.appendChild(doc.createTextNode(text));
    if (detail) node.appendChild(el('span', { 'class': 'status-detail', text: detail }));
  }

  function key(name, vars) {
    return function (lang) {
      return t(lang, name, vars);
    };
  }

  function described(failure, context) {
    return function (lang) {
      return explain(failure, lang, context);
    };
  }

  function statusOf(scope) {
    return $('.form-foot [data-status]', scope) || $('[data-status]', scope);
  }

  function fieldInput(scope, path) {
    return $$('[data-field]', scope).filter(function (node) {
      return node.getAttribute('data-field') === path;
    })[0] || null;
  }

  function fieldError(scope, path, describe) {
    var input = fieldInput(scope, path);
    if (!input) return null;
    input.setAttribute('aria-invalid', 'true');
    var box = input.closest('.field') || input.parentNode;
    var note = $('.field-error', box);
    if (!note) {
      note = el('p', { 'class': 'field-error', id: nextId('error') });
      box.appendChild(note);
      if (!input.hasAttribute('data-described')) {
        input.setAttribute('data-described', input.getAttribute('aria-describedby') || '');
      }
      input.setAttribute('aria-describedby', ((input.getAttribute('data-described') || '') + ' ' + note.id).trim());
    }
    say(note, 'error', describe);
    var details = input.closest('details');
    if (details) details.open = true;
    return input;
  }

  function clearField(input) {
    input.removeAttribute('aria-invalid');
    if (input.hasAttribute('data-described')) {
      var before = input.getAttribute('data-described');
      if (before) input.setAttribute('aria-describedby', before);
      else input.removeAttribute('aria-describedby');
      input.removeAttribute('data-described');
    }
    var box = input.closest('.field') || input.parentNode;
    var note = box && $('.field-error', box);
    if (note) note.parentNode.removeChild(note);
  }

  function clearErrors(scope) {
    $$('[aria-invalid]', scope).forEach(clearField);
    $$('.field-error', scope).forEach(function (note) {
      note.parentNode.removeChild(note);
    });
    say(statusOf(scope), null, null);
  }

  function showFailure(scope, failure, context) {
    var shown = explain(failure, state.lang, context);
    var input = shown.field ? fieldError(scope, shown.field, function (lang) {
      return explain(failure, lang, context).short || explain(failure, lang, context).text;
    }) : null;
    say(statusOf(scope), 'error', described(failure, context));
    if (input) input.focus();
  }

  function showRuleErrors(scope, prefix, errors) {
    var first = null;
    errors.forEach(function (error) {
      var input = fieldError(scope, prefix + error.name, function (lang) {
        return sentence(t(lang, error.rule.key, error.rule.vars));
      });
      first = first || input;
    });
    say(statusOf(scope), 'error', key('form.check'));
    if (first) first.focus();
  }

  function pending(form, on) {
    $$('button[type="submit"], [data-action]', form).forEach(function (button) {
      if (on) {
        button.setAttribute('data-was-disabled', button.disabled ? '1' : '');
        button.disabled = true;
      } else if (button.hasAttribute('data-was-disabled')) {
        button.disabled = button.getAttribute('data-was-disabled') === '1';
        button.removeAttribute('data-was-disabled');
      }
    });
  }

  function failure(status, code, message) {
    return { failure: true, status: status, code: code || 'http', message: message || '' };
  }

  function request(method, path, body, timeoutMs) {
    var controller = typeof window.AbortController === 'function' ? new window.AbortController() : null;
    var timer = controller ? window.setTimeout(function () {
      controller.abort();
    }, timeoutMs || REQUEST_MS) : null;
    var options = { method: method, credentials: 'same-origin', cache: 'no-store', headers: { Accept: 'application/json' } };
    if (method !== 'GET') {
      options.headers['Content-Type'] = 'application/json';
      options.body = JSON.stringify(body || {});
    }
    if (controller) options.signal = controller.signal;
    return window.fetch(path, options).then(function (resp) {
      reached();
      return resp.json().then(null, function () {
        return null;
      }).then(function (data) {
        if (resp.ok) return data || {};
        throw failure(resp.status, data && data.error, data && data.message);
      });
    }, function (e) {
      throw failure(0, e && e.name === 'AbortError' ? 'timeout' : 'network', '');
    }).then(function (data) {
      if (timer) window.clearTimeout(timer);
      return data;
    }, function (f) {
      if (timer) window.clearTimeout(timer);
      throw f && f.failure ? f : failure(0, 'network', '');
    });
  }

  function reached() {
    var banner = $('[data-banner="offline"]');
    if (banner) banner.hidden = true;
  }

  function never() {
    return new Promise(function () {});
  }

  function report(e) {
    if (e && !e.failure) {
      window.setTimeout(function () {
        throw e;
      }, 0);
    }
  }

  function call(method, path, body, timeoutMs) {
    var session = state.session;
    return request(method, path, body, timeoutMs).then(function (data) {
      return session === state.session ? data : never();
    }, function (f) {
      if (session !== state.session) return never();
      if (f.code === 'foreign_host') showHost();
      else if (f.code === 'login_required' && state.screen === 'main') expire();
      if (session !== state.session) return never();
      throw f;
    });
  }

  function speakerPath(name, tail) {
    return 'api/devices/' + encodeURIComponent(name) + (tail || '');
  }

  function show(screen) {
    state.screen = screen;
    $$('[data-screen]').forEach(function (node) {
      node.hidden = node.getAttribute('data-screen') !== screen;
    });
    $$('[data-only-main]').forEach(function (node) {
      node.hidden = screen !== 'main';
    });
    if (screen !== 'main') stopBusyTimer();
  }

  function showHost() {
    var host = window.location.host;
    say($('[data-host-opened]'), null, key('host.opened', { host: host }));
    $('[data-hosts-line]').textContent = 'VOICE_HUB_HOSTS=' + window.location.hostname;
    show('host');
  }

  function showSetup() {
    show('setup');
    $('#setup-password').focus();
  }

  function showOffline() {
    show('offline');
  }

  function markLoginHints() {
    var env = !!(state.auth && state.auth.password_from_env);
    $('[data-login-env]').hidden = !env;
    $('[data-login-forgot]').hidden = env;
  }

  function showLogin(note) {
    markLoginHints();
    say($('[data-login-note]'), null, note ? key(note) : null);
    $('[data-login-note]').hidden = !note;
    clearErrors($('[data-form="login"]'));
    show('login');
    var input = $('#login-password');
    if (input) input.focus();
  }

  function forget() {
    state.session += 1;
    state.settings = null;
    state.drafts = null;
    state.devices = [];
    state.found = [];
    state.foundShown = null;
    state.loadingSettings = false;
    state.loadingDevices = false;
    state.loadingFound = false;
    $('[data-banner="settings"]').hidden = true;
    cards.forEach(function (card) {
      if (card.root.parentNode) card.root.parentNode.removeChild(card.root);
    });
    cards.clear();
    $$('form').forEach(function (form) {
      pending(form, false);
      form.reset();
      clearErrors(form);
    });
    $$('[data-status]').forEach(function (node) {
      say(node, null, null);
    });
    lockUnloaded();
    markUnsaved();
  }

  function expire() {
    if (state.auth) state.auth.logged_in = false;
    var drafts = keepDrafts();
    forget();
    state.drafts = drafts;
    showLogin('login.expired');
    request('GET', 'api/state').then(function (auth) {
      state.auth = auth;
      if (state.screen !== 'login') return;
      if (!auth.password_set) showSetup();
      else markLoginHints();
    }, function () {});
  }

  function boot() {
    show('loading');
    request('GET', 'api/state').then(function (auth) {
      state.auth = auth;
      if (!auth.password_set) showSetup();
      else if (!auth.logged_in) showLogin(null);
      else enterMain();
    }, function (f) {
      if (f.code === 'foreign_host') showHost();
      else showOffline();
    });
  }

  function enterMain() {
    show('main');
    selectTab(tabFromHash());
    $('[data-password-env]').hidden = !state.auth.password_from_env;
    $('[data-password-fields]').hidden = !!state.auth.password_from_env;
    if (state.busy) runBusyTimer();
    loadSettings();
    refreshDevices();
    refreshFound();
  }

  function tabFromHash() {
    var name = String(window.location.hash || '').replace('#', '');
    return TABS.indexOf(name) >= 0 ? name : 'speakers';
  }

  function selectTab(name) {
    state.tab = name;
    $$('[data-tab]').forEach(function (node) {
      node.hidden = node.getAttribute('data-tab') !== name;
    });
    $$('[data-tab-link]').forEach(function (link) {
      if (link.getAttribute('data-tab-link') === name) link.setAttribute('aria-current', 'page');
      else link.removeAttribute('aria-current');
    });
    if (name === 'speakers' && state.screen === 'main') {
      refreshDevices();
      refreshFound();
    }
  }

  function openTab(name) {
    if (tabFromHash() !== name) {
      var history = window.history;
      if (history && typeof history.pushState === 'function') history.pushState(null, '', '#' + name);
      else window.location.hash = '#' + name;
    }
    selectTab(name);
  }

  function loadSettings() {
    if (state.loadingSettings || state.screen !== 'main') return;
    state.loadingSettings = true;
    call('GET', 'api/settings').then(function (data) {
      if (state.screen !== 'main') return;
      $('[data-banner="settings"]').hidden = true;
      takeSettings(data);
      Object.keys(SPECS).forEach(function (name) {
        fillForm(name);
      });
      restoreDrafts();
      markUnsaved();
    }).then(null, function (e) {
      report(e);
      if (state.screen === 'main' && !state.settings) $('[data-banner="settings"]').hidden = false;
    }).then(function () {
      state.loadingSettings = false;
    });
  }

  function keepDrafts() {
    if (!state.settings) return state.drafts;
    var drafts = {};
    Object.keys(SPECS).forEach(function (name) {
      var form = $('[data-form="' + name + '"]');
      if (!form) return;
      var specs = specsOf(name);
      var changed = changedValues(specs, loadedSection(name), formValues(form, prefixOf(name), specs));
      if (Object.keys(changed).length) drafts[name] = changed;
    });
    return Object.keys(drafts).length ? drafts : null;
  }

  function specsOf(name) {
    if (name !== 'server' || !state.settings || !state.settings.http_port_from_env) return SPECS[name];
    return SPECS.server.filter(function (spec) {
      return spec.name !== 'http_port';
    });
  }

  function unsavedForms() {
    if (!state.settings) return [];
    return Object.keys(SPECS).filter(function (name) {
      var form = $('[data-form="' + name + '"]');
      if (!form) return false;
      var specs = specsOf(name);
      var values = formValues(form, prefixOf(name), specs);
      var typed = specs.some(function (spec) {
        return spec.kind === 'secret' && String(values[spec.name] || '').trim() !== '';
      });
      return typed || Object.keys(changedValues(specs, loadedSection(name), values)).length > 0;
    });
  }

  function markUnsaved() {
    var dirty = unsavedForms();
    Object.keys(SPECS).forEach(function (name) {
      var mark = $('[data-form="' + name + '"] [data-unsaved]');
      if (mark) mark.hidden = dirty.indexOf(name) < 0;
    });
  }

  function lockUnloaded() {
    $$('[data-needs-settings]').forEach(function (button) {
      if (button.hasAttribute('data-was-disabled')) button.setAttribute('data-was-disabled', state.settings ? '' : '1');
      else button.disabled = !state.settings;
    });
  }

  function restoreDrafts() {
    var drafts = state.drafts;
    state.drafts = null;
    if (!drafts) return;
    Object.keys(drafts).forEach(function (name) {
      var form = $('[data-form="' + name + '"]');
      Object.keys(drafts[name]).forEach(function (field) {
        var input = form && fieldInput(form, prefixOf(name) + field);
        if (!input) return;
        if (input.type === 'checkbox') input.checked = drafts[name][field] === true;
        else input.value = drafts[name][field];
      });
    });
    updateLanguageNote();
  }

  function takeSettings(data) {
    state.settings = data.settings;
    state.restartNeeded = !!data.restart_needed;
    lockUnloaded();
    renderSettingsState();
  }

  function renderSettingsState() {
    var settings = state.settings;
    if (!settings) return;
    var keySet = !!settings.openai.api_key_set;
    var pill = $('[data-key-state]');
    pill.textContent = tr(keySet ? 'openai.key_set' : 'openai.key_unset');
    pill.setAttribute('data-tone', keySet ? 'ok' : 'warn');
    $('#openai-key').setAttribute('placeholder', tr(keySet ? 'openai.key_placeholder_set' : 'openai.key_placeholder'));
    $('[data-action="clear-key"]').hidden = !keySet;
    $('[data-banner="nokey"]').hidden = keySet;
    $('[data-banner="restart"]').hidden = !state.restartNeeded;
    $$('[data-applies]').forEach(function (node) {
      node.textContent = tr('conversation.applies');
    });
    var portEnv = !!settings.http_port_from_env;
    if (portEnv) $('#server-port').setAttribute('readonly', '');
    else $('#server-port').removeAttribute('readonly');
    $('#server-port-hint').setAttribute('data-hint', portEnv ? 'server.port_env' : 'server.port_hint');
    renderHints();
    updateLanguageNote();
  }

  function formValues(form, prefix, specs) {
    var values = {};
    specs.forEach(function (spec) {
      var input = fieldInput(form, prefix + spec.name);
      if (!input) return;
      values[spec.name] = input.type === 'checkbox' ? input.checked : input.value;
    });
    return values;
  }

  function loadedSection(name) {
    var settings = state.settings;
    if (name === 'server') return { http_port: settings.http_port, advertise_host: settings.advertise_host };
    return settings[name];
  }

  function prefixOf(name) {
    return name === 'server' ? '' : name + '.';
  }

  function fillValue(input, kind, value) {
    if (input.type === 'checkbox') input.checked = value === true;
    else if (kind === 'choice' && input.tagName === 'SELECT') {
      var wanted = value === null || value === undefined ? '' : String(value);
      var found = Array.prototype.some.call(input.options, function (option) {
        return option.value === wanted;
      });
      if (!found) input.appendChild(el('option', { value: wanted, text: wanted }));
      input.value = wanted;
    } else {
      var text = value === null || value === undefined ? '' : String(value);
      if (typeof value === 'number') text = formatNumber(value, state.lang);
      if (input.value !== text) input.value = text;
    }
  }

  function fillForm(name, expected, sent) {
    var form = $('[data-form="' + name + '"]');
    var loaded = loadedSection(name);
    if (!form || !loaded) return;
    var kept = expected ? changedValues(SPECS[name], expected, formValues(form, prefixOf(name), SPECS[name])) : {};
    SPECS[name].forEach(function (spec) {
      var input = fieldInput(form, prefixOf(name) + spec.name);
      var chosenByServer = sent && has(sent, spec.name) && sent[spec.name] === null;
      if (!input || (has(kept, spec.name) && !chosenByServer)) return;
      if (spec.kind === 'secret') input.value = '';
      else fillValue(input, spec.kind, loaded[spec.name]);
      clearField(input);
    });
    if (name === 'assistant') updateLanguageNote();
  }

  function withPatch(loaded, patch) {
    var out = {};
    [loaded, patch].forEach(function (part) {
      Object.keys(part).forEach(function (name) {
        out[name] = part[name];
      });
    });
    return out;
  }

  function saveSection(name, patch, form, done) {
    var body = {};
    if (name === 'server') body = patch;
    else body[name] = patch;
    var expected = withPatch(loadedSection(name), patch);
    pending(form, true);
    say(statusOf(form), 'wait', key('saving'));
    return call('PUT', 'api/settings', body).then(function (data) {
      var before = state.settings;
      takeSettings(data);
      fillForm(name, expected, patch);
      markUnsaved();
      if (done) done(before, data.settings);
      else say(statusOf(form), 'ok', key('saved'));
    }, function (f) {
      showFailure(form, f);
    }).then(function () {
      pending(form, false);
    });
  }

  function needSettings(form) {
    if (state.settings) return true;
    say(statusOf(form), 'error', key('settings.not_loaded'));
    loadSettings();
    return false;
  }

  function submitSection(name, form) {
    clearErrors(form);
    if (!needSettings(form)) return;
    var specs = specsOf(name);
    var built = buildPatch(specs, loadedSection(name), formValues(form, prefixOf(name), specs));
    if (built.errors.length) {
      showRuleErrors(form, prefixOf(name), built.errors);
      return;
    }
    if (built.empty) {
      say(statusOf(form), null, key('nothing'));
      return;
    }
    if (name === 'assistant') {
      saveSection(name, built.patch, form, function (before, after) {
        var switched = has(built.patch, 'language') && !has(built.patch, 'instructions');
        if (!switched) say(statusOf(form), 'ok', key('saved'));
        else if (after.assistant.instructions !== before.assistant.instructions) {
          say(statusOf(form), 'ok', key('assistant.language_switched'));
        } else say(statusOf(form), 'ok', key('assistant.language_kept'));
      });
    } else if (name === 'server') {
      saveSection(name, built.patch, form, function () {
        say(statusOf(form), 'ok', key(state.restartNeeded ? 'server.restart' : 'saved_plain'));
      });
    } else saveSection(name, built.patch, form);
  }

  function updateLanguageNote() {
    var select = $('#assistant-language');
    var note = $('[data-language-note]');
    if (!select || !note) return;
    note.hidden = !state.settings || select.value === state.settings.assistant.language;
  }

  function clearKey() {
    var form = $('[data-form="openai"]');
    if (!needSettings(form)) return;
    confirmAction(key('openai.key_clear_confirm'), 'openai.key_clear_ok').then(function (yes) {
      if (!yes || !state.settings) return;
      clearErrors(form);
      saveSection('openai', { api_key: null }, form, function () {
        say(statusOf(form), 'ok', key('openai.key_cleared'));
      });
    });
  }

  function resetInstructions() {
    var form = $('[data-form="assistant"]');
    if (!needSettings(form)) return;
    var language = $('#assistant-language').value;
    var langKey = 'lang.' + language;
    confirmAction(function (lang) {
      return t(lang, 'assistant.reset_confirm', { lang: t(lang, langKey) });
    }, 'assistant.reset_ok').then(function (yes) {
      if (!yes || !state.settings) return;
      clearErrors(form);
      saveSection('assistant', { language: language, instructions: null }, form, function () {
        say(statusOf(form), 'ok', function (lang) {
          return t(lang, 'assistant.reset_done', { lang: t(lang, langKey) });
        });
      });
    });
  }

  function fillTimingDefaults() {
    var form = $('[data-form="timing"]');
    clearErrors(form);
    TIMING_FIELDS.forEach(function (field) {
      var input = fieldInput(form, 'timing.' + field.name);
      if (input) input.value = formatNumber(DEFAULTS.timing[field.name], state.lang);
    });
    markUnsaved();
    say(statusOf(form), null, key('timing.defaults_done'));
  }

  function submitSetup(form) {
    clearErrors(form);
    var password = fieldInput(form, 'password').value;
    var repeat = fieldInput(form, 'repeat').value;
    if (password.length < MIN_PASSWORD) return showRuleErrors(form, '', [{ name: 'password', rule: { key: 'password.short' } }]);
    if (password !== repeat) return showRuleErrors(form, '', [{ name: 'repeat', rule: { key: 'password.mismatch' } }]);
    pending(form, true);
    say(statusOf(form), 'wait', key('saving'));
    request('POST', 'api/setup', { password: password, language: state.lang }).then(function (auth) {
      state.auth = auth;
      form.reset();
      clearErrors(form);
      enterMain();
    }, function (f) {
      if (f.code === 'password_set') {
        state.auth.password_set = true;
        form.reset();
        showLogin('err.password_set');
      } else if (f.code === 'foreign_host') showHost();
      else showFailure(form, f);
    }).then(function () {
      pending(form, false);
    });
  }

  function submitLogin(form) {
    clearErrors(form);
    var input = fieldInput(form, 'password');
    if (!input.value) return showRuleErrors(form, '', [{ name: 'password', rule: { key: 'login.empty' } }]);
    pending(form, true);
    say(statusOf(form), 'wait', key('login.checking'));
    request('POST', 'api/login', { password: input.value }).then(function (auth) {
      state.auth = auth;
      form.reset();
      clearErrors(form);
      enterMain();
    }, function (f) {
      if (f.code === 'no_password') {
        state.auth.password_set = false;
        form.reset();
        showSetup();
      } else if (f.code === 'foreign_host') showHost();
      else {
        input.value = '';
        showFailure(form, f);
      }
    }).then(function () {
      pending(form, false);
    });
  }

  function submitPassword(form) {
    clearErrors(form);
    var current = fieldInput(form, 'current').value;
    var fresh = fieldInput(form, 'new').value;
    var repeat = fieldInput(form, 'repeat').value;
    if (!current) return showRuleErrors(form, '', [{ name: 'current', rule: { key: 'password.empty' } }]);
    if (fresh.length < MIN_PASSWORD) return showRuleErrors(form, '', [{ name: 'new', rule: { key: 'password.short' } }]);
    if (fresh !== repeat) return showRuleErrors(form, '', [{ name: 'repeat', rule: { key: 'password.mismatch' } }]);
    pending(form, true);
    say(statusOf(form), 'wait', key('saving'));
    call('POST', 'api/password', { current: current, 'new': fresh }).then(function (auth) {
      state.auth = auth;
      form.reset();
      say(statusOf(form), 'ok', key('password.changed'));
    }, function (f) {
      if (f.code === 'wrong_password') {
        fieldInput(form, 'current').value = '';
        fieldError(form, 'current', function (lang) {
          return explain(f, lang, 'password').text;
        }).focus();
        say(statusOf(form), 'error', described(f, 'password'));
      } else showFailure(form, f, 'password');
    }).then(function () {
      pending(form, false);
    });
  }

  function logout() {
    var status = $('[data-status="logout"]');
    say(status, null, null);
    request('POST', 'api/logout', {}).then(function (auth) {
      state.auth = auth;
      forget();
      showLogin('login.loggedout');
    }, function (f) {
      if (f.code === 'login_required') {
        forget();
        showLogin('login.loggedout');
      } else if (f.code === 'foreign_host') showHost();
      else {
        openTab('access');
        say(status, 'error', described(f));
      }
    });
  }

  function setDevices(devices) {
    state.devices = Array.isArray(devices) ? devices : [];
    renderDevices();
    renderFound(false);
  }

  function refreshDevices() {
    if (state.loadingDevices || state.screen !== 'main') return;
    state.loadingDevices = true;
    call('GET', 'api/devices').then(function (data) {
      setDevices(data.devices);
    }).then(null, function (f) {
      report(f);
      if (f.code === 'network' || f.code === 'timeout') $('[data-banner="offline"]').hidden = false;
    }).then(function () {
      state.loadingDevices = false;
    });
  }

  function refreshFound() {
    if (state.loadingFound || state.screen !== 'main') return;
    state.loadingFound = true;
    call('GET', 'api/discover').then(function (data) {
      state.found = Array.isArray(data.found) ? data.found : [];
      renderFound(false);
    }).then(null, report).then(function () {
      state.loadingFound = false;
    });
  }

  function buildSpeaker(name) {
    var wakeId = nextId('wake');
    var volumeId = nextId('volume');
    var hostId = nextId('host');
    var portId = nextId('port');
    var card = {
      name: name, active: null, saving: false, removing: false, moving: false, keyed: false, signature: null,
      volumeDragging: false, volumeSaving: false, volumeNext: null, volumeSaid: null
    };
    card.nameText = el('span', { 'class': 'speaker-name', text: name });
    card.pill = el('span', { 'class': 'pill' });
    card.address = el('p', { 'class': 'speaker-address' });
    card.hint = el('p', { 'class': 'hint' });
    card.wake = el('select', { id: wakeId, 'data-field': 'wake_word', 'data-busy-off': true });
    card.wakeNote = el('p', { 'class': 'hint' });
    card.volume = el('input', {
      id: volumeId, type: 'range', min: '0', max: '100', step: '1', 'data-field': 'volume', 'data-busy-off': true
    });
    card.volumeValue = el('span', { 'class': 'volume-value', 'aria-hidden': 'true' });
    card.moveButton = el('button', { type: 'button', 'data-busy-off': true, i18n: 'speaker.move' });
    card.removeButton = el('button', { type: 'button', 'class': 'danger', 'data-busy-off': true, i18n: 'speaker.remove' });
    card.moveHost = el('input', { id: hostId, type: 'text', 'data-field': 'host', autocomplete: 'off', spellcheck: 'false', autocapitalize: 'off' });
    card.movePort = el('input', { id: portId, type: 'text', inputmode: 'numeric', 'data-field': 'port', autocomplete: 'off' });
    card.moveForm = el('form', { 'class': 'move-form', method: 'post', novalidate: true, hidden: true }, [
      el('div', { 'class': 'field-row' }, [
        el('div', { 'class': 'field grow' }, [el('label', { 'for': hostId, i18n: 'speaker.new_host' }), card.moveHost]),
        el('div', { 'class': 'field port' }, [el('label', { 'for': portId, i18n: 'manual.port' }), card.movePort])
      ]),
      el('p', { 'class': 'hint', i18n: 'speaker.move_hint' }),
      el('div', { 'class': 'form-foot' }, [
        el('button', { type: 'submit', 'class': 'primary', 'data-busy-off': true, i18n: 'speaker.move_save' }),
        el('button', { type: 'button', 'data-move-cancel': true, i18n: 'cancel' }),
        el('p', { 'class': 'status', 'data-status': true, role: 'status', 'aria-live': 'polite' })
      ])
    ]);
    card.status = el('p', { 'class': 'status', 'data-status': true, role: 'status', 'aria-live': 'polite' });
    card.root = el('li', { 'class': 'speaker' }, [
      el('div', { 'class': 'speaker-head' }, [card.nameText, card.pill]),
      card.address,
      card.hint,
      el('div', { 'class': 'field' }, [el('label', { 'for': wakeId, i18n: 'speaker.wake' }), card.wake, card.wakeNote]),
      el('div', { 'class': 'field' }, [
        el('label', { 'for': volumeId, i18n: 'speaker.volume' }),
        el('div', { 'class': 'volume-row' }, [card.volume, card.volumeValue]),
        el('p', { 'class': 'hint', i18n: 'speaker.volume_hint' })
      ]),
      el('div', { 'class': 'speaker-actions' }, [card.moveButton, card.removeButton]),
      card.moveForm,
      card.status
    ]);
    card.wake.addEventListener('mousedown', function () {
      card.keyed = false;
    });
    card.wake.addEventListener('keydown', function (event) {
      if (event.key !== 'Enter') {
        card.keyed = true;
        return;
      }
      card.keyed = false;
      changeWake(card);
    });
    card.wake.addEventListener('change', function () {
      if (!card.keyed) changeWake(card);
    });
    card.wake.addEventListener('blur', function () {
      card.keyed = false;
      changeWake(card);
    });
    card.volume.addEventListener('input', function () {
      if (card.volume.disabled) return;
      card.volumeDragging = true;
      showVolume(card, Number(card.volume.value));
    });
    card.volume.addEventListener('change', function () {
      card.volumeDragging = false;
      changeVolume(card);
    });
    ['pointerup', 'mouseup', 'touchend', 'touchcancel', 'blur'].forEach(function (type) {
      card.volume.addEventListener(type, function () {
        card.volumeDragging = false;
      });
    });
    card.moveButton.addEventListener('click', function () {
      openMove(card);
    });
    card.removeButton.addEventListener('click', function () {
      removeSpeaker(card);
    });
    $('[data-move-cancel]', card.moveForm).addEventListener('click', function () {
      card.moveForm.hidden = true;
      clearErrors(card.moveForm);
      card.moveButton.focus();
    });
    card.moveForm.addEventListener('submit', function (event) {
      event.preventDefault();
      moveSpeaker(card);
    });
    return card;
  }

  function deviceOf(name) {
    return state.devices.filter(function (d) {
      return d.name === name;
    })[0] || null;
  }

  function updateSpeaker(card, device) {
    var words = device.wake_words || { available: [], active: [] };
    var available = words.available || [];
    var active = (words.active || [])[0] || null;
    var connected = !!device.connected;
    card.device = device;
    card.address.textContent = device.host + ':' + device.port;
    card.pill.textContent = tr(connected ? 'speaker.online' : device.key_set ? 'speaker.offline' : 'speaker.nokey');
    card.pill.setAttribute('data-tone', connected ? 'ok' : 'warn');
    var hint = connected ? '' : tr(device.key_set ? 'speaker.offline_hint' : 'speaker.nokey_hint');
    card.hint.textContent = hint;
    card.hint.hidden = !hint;
    var signature = JSON.stringify([available, active, connected]);
    if (!card.saving && signature !== card.signature) {
      card.signature = signature;
      while (card.wake.firstChild) card.wake.removeChild(card.wake.firstChild);
      if (!available.length) card.wake.appendChild(el('option', { value: '', text: '—' }));
      available.forEach(function (word) {
        card.wake.appendChild(el('option', { value: word.id, text: word.phrase || word.id }));
      });
      if (active && !available.some(function (word) { return word.id === active; })) {
        card.wake.appendChild(el('option', { value: active, text: active }));
      }
      card.wake.value = active || '';
      card.active = active;
    }
    var locks = speakerLocks(device, card, state.busy);
    card.wake.disabled = locks.wake;
    var note = !connected ? 'speaker.wake_offline' : !available.length ? 'speaker.wake_none' : '';
    card.wakeNote.textContent = note ? tr(note) : '';
    card.wakeNote.hidden = !note;
    card.volume.disabled = locks.volume;
    if (locks.volume) card.volumeDragging = false;
    if (!card.volumeDragging && !card.volumeSaving) {
      var level = volumeOf(device);
      if (level !== null) card.volume.value = String(level);
      showVolume(card, level);
    }
    card.moveButton.disabled = locks.move;
    card.removeButton.disabled = locks.remove;
    $$('button[type="submit"]', card.moveForm).forEach(function (button) {
      button.disabled = locks.move;
    });
  }

  function relock(card) {
    if (card.device) updateSpeaker(card, card.device);
  }

  function renderDevices() {
    var list = $('[data-speaker-list]');
    var seen = new Set();
    state.devices.forEach(function (device, i) {
      seen.add(device.name);
      var card = cards.get(device.name);
      if (!card) {
        card = buildSpeaker(device.name);
        cards.set(device.name, card);
      }
      updateSpeaker(card, device);
      if (list.children[i] !== card.root) list.insertBefore(card.root, list.children[i] || null);
    });
    cards.forEach(function (card, name) {
      if (seen.has(name)) return;
      if (card.root.parentNode) card.root.parentNode.removeChild(card.root);
      cards.delete(name);
    });
    $('[data-speakers-empty]').hidden = state.devices.length > 0;
  }

  function changeWake(card) {
    var id = card.wake.value;
    var previous = card.active;
    if (card.saving || card.wake.disabled || !id || id === previous) return;
    var phrase = id;
    ((card.device && card.device.wake_words && card.device.wake_words.available) || []).forEach(function (word) {
      if (word.id === id) phrase = word.phrase || id;
    });
    var focused = doc.activeElement === card.wake;
    card.saving = true;
    card.wake.disabled = true;
    say(card.status, 'wait', key('speaker.wake_saving'));
    call('PUT', speakerPath(card.name, '/wake-word'), { wake_word: id }).then(function (data) {
      card.saving = false;
      card.signature = null;
      setDevices(data.devices);
      say(card.status, 'ok', key('speaker.wake_saved', { phrase: phrase }));
    }, function (f) {
      card.saving = false;
      card.signature = null;
      card.wake.value = previous || '';
      var target = f.code === 'unknown_speaker' ? $('[data-status="mine"]') : card.status;
      say(target, 'error', described(f));
      if (card.device) updateSpeaker(card, card.device);
      if (f.code === 'unknown' || f.code === 'unknown_speaker' || f.code === 'offline') refreshDevices();
    }).then(function () {
      if (focused && !card.wake.disabled && cards.get(card.name) === card) card.wake.focus();
    });
  }

  function showVolume(card, level) {
    if (level === null) {
      card.volumeValue.textContent = '—';
      card.volume.removeAttribute('aria-valuetext');
      return;
    }
    var shown = tr('speaker.volume_value', { n: formatNumber(level, state.lang) });
    card.volumeValue.textContent = shown;
    card.volume.setAttribute('aria-valuetext', shown);
  }

  function changeVolume(card) {
    var level = Number(card.volume.value);
    if (card.volume.disabled || !isFinite(level)) return;
    if (card.volumeSaving) card.volumeNext = level;
    else if (level !== volumeOf(card.device)) saveVolume(card, level);
  }

  function saveVolume(card, level) {
    card.volumeSaving = true;
    card.volumeNext = null;
    call('PUT', speakerPath(card.name, '/volume'), { volume: level }).then(function (data) {
      var next = card.volumeNext;
      card.volumeSaving = false;
      card.volumeNext = null;
      if (card.status._say && card.status._say.describe === card.volumeSaid) say(card.status, null, null);
      if (next !== null && next !== level && cards.get(card.name) === card) saveVolume(card, next);
      setDevices(data.devices);
    }, function (f) {
      card.volumeSaving = false;
      card.volumeNext = null;
      var gone = f.code === 'unknown_speaker';
      card.volumeSaid = described(f, 'volume');
      say(gone ? $('[data-status="mine"]') : card.status, 'error', card.volumeSaid);
      if (card.device) updateSpeaker(card, card.device);
      if (gone || f.code === 'offline') refreshDevices();
    });
  }

  function openMove(card) {
    clearErrors(card.moveForm);
    card.moveHost.value = card.device ? card.device.host : '';
    card.movePort.value = card.device ? String(card.device.port) : String(DEFAULTS.port);
    card.moveForm.hidden = false;
    card.moveHost.focus();
  }

  function addressFrom(form, hostInput, portInput) {
    var errors = [];
    var host = hostInput.value.trim();
    if (!host) errors.push({ name: 'host', rule: { key: 'rule.empty' } });
    else if (!isHost(host)) errors.push({ name: 'host', rule: KIND_RULES.host });
    var portText = portInput.value.trim();
    var port = DEFAULTS.port;
    if (portText) {
      var got = checkValue('port', portText);
      if (got.rule) errors.push({ name: 'port', rule: got.rule });
      else port = got.value;
    }
    if (errors.length) {
      showRuleErrors(form, '', errors);
      return null;
    }
    return { host: host, port: port };
  }

  function moveSpeaker(card) {
    var form = card.moveForm;
    clearErrors(form);
    var address = addressFrom(form, card.moveHost, card.movePort);
    if (!address || card.moving) return;
    card.moving = true;
    relock(card);
    say(statusOf(form), 'wait', key('saving'));
    call('PUT', speakerPath(card.name), address).then(function (data) {
      card.moving = false;
      form.hidden = true;
      say(statusOf(form), null, null);
      setDevices(data.devices);
      say(card.status, 'ok', key('speaker.moved'));
    }, function (f) {
      card.moving = false;
      relock(card);
      if (f.code === 'unknown_speaker') {
        say($('[data-status="mine"]'), 'error', described(f));
        refreshDevices();
      } else showFailure(form, f);
    });
  }

  function removeSpeaker(card) {
    var name = card.name;
    confirmAction(key(removeQuestion(card.device), { name: name }), 'speaker.remove_ok').then(function (yes) {
      if (!yes || card.removing) return;
      card.removing = true;
      relock(card);
      call('DELETE', speakerPath(name), {}).then(function (data) {
        card.removing = false;
        setDevices(data.devices);
        say($('[data-status="mine"]'), 'ok', key('speaker.removed', { name: name }));
        refreshFound();
      }, function (f) {
        card.removing = false;
        var gone = f.code === 'unknown_speaker' || !cards.has(name);
        say(gone ? $('[data-status="mine"]') : card.status, 'error', described(f));
        if (gone) refreshDevices();
        relock(card);
      });
    });
  }

  function foundItem(entry) {
    var found = entry.found;
    var itemKey = found.name + '@' + found.host + ':' + found.port;
    var action;
    if (entry.adopt) {
      action = el('button', {
        type: 'button',
        'class': entry.reflashed ? null : 'primary',
        'data-busy-off': true,
        'data-found-key': itemKey + '#adopt',
        i18n: entry.reflashed ? 'found.adopt_again' : 'found.adopt'
      });
      action.addEventListener('click', function () {
        adoptFound(found);
      });
    } else {
      action = el('button', { type: 'button', 'data-busy-off': true, 'data-found-key': itemKey + '#manual', i18n: 'found.add_manual' });
      action.addEventListener('click', function () {
        prefillManual(found);
      });
    }
    action.disabled = !!state.busy;
    var title = found.friendly_name && found.friendly_name !== found.name ? found.friendly_name : found.name;
    var address = (title !== found.name ? found.name + ' · ' : '') + found.host + ':' + found.port;
    return el('li', { 'class': 'found' }, [
      el('div', { 'class': 'found-head' }, [
        el('span', { 'class': 'found-name', text: title }),
        entry.adopt && !entry.reflashed ? el('span', { 'class': 'pill', 'data-tone': 'warn', i18n: 'found.waiting' }) : null
      ]),
      el('p', { 'class': 'found-address', text: address }),
      entry.reflashed ? el('p', { 'class': 'hint', i18n: 'found.reflashed' }) : null,
      el('div', { 'class': 'found-actions' }, [action])
    ]);
  }

  function renderFound(force) {
    var sorted = sortFound(state.found, state.devices);
    var shown = JSON.stringify([sorted, !!state.busy, state.lang]);
    if (!force && shown === state.foundShown) return;
    state.foundShown = shown;
    var list = $('[data-found-list]');
    var others = $('[data-found-others-list]');
    var focused = doc.activeElement && doc.activeElement.hasAttribute && doc.activeElement.hasAttribute('data-found-key') ?
      doc.activeElement.getAttribute('data-found-key') : null;
    [list, others].forEach(function (node) {
      while (node.firstChild) node.removeChild(node.firstChild);
    });
    var waiting = sorted.waiting;
    var keyed = sorted.others;
    waiting.forEach(function (found) {
      list.appendChild(foundItem(found));
    });
    keyed.forEach(function (found) {
      others.appendChild(foundItem(found));
    });
    $('[data-found-empty]').hidden = waiting.length > 0;
    $('[data-found-others]').hidden = keyed.length === 0;
    $('[data-found-others-title]').textContent = tr('found.others', { n: keyed.length });
    if (focused) {
      var again = $$('[data-found-key]').filter(function (node) {
        return node.getAttribute('data-found-key') === focused;
      })[0];
      if (again && !again.disabled) again.focus();
    }
  }

  function prefillManual(found) {
    var form = $('[data-form="manual"]');
    clearErrors(form);
    fieldInput(form, 'host').value = found.host;
    fieldInput(form, 'port').value = String(found.port);
    say(statusOf(form), null, key('manual.filled'));
    var input = fieldInput(form, 'key');
    if (typeof form.scrollIntoView === 'function') form.scrollIntoView({ block: 'start', behavior: 'smooth' });
    input.focus({ preventScroll: true });
  }

  function startBusy(kind, host) {
    state.busy = { kind: kind, host: host, since: Date.now() };
    runBusyTimer();
    renderDevices();
    renderFound(true);
  }

  function runBusyTimer() {
    stopBusyTimer();
    state.busyTimer = window.setInterval(renderBusy, 1000);
    renderBusy();
  }

  function stopBusyTimer() {
    if (state.busyTimer) window.clearInterval(state.busyTimer);
    state.busyTimer = null;
  }

  function endBusy() {
    state.busy = null;
    stopBusyTimer();
    renderBusy();
    renderDevices();
    renderFound(true);
  }

  function renderBusy() {
    var busy = state.busy;
    var box = $('[data-progress]');
    box.hidden = !busy;
    $$('[data-busy-off]').forEach(function (node) {
      if (node.closest('.speaker') || node.closest('.found')) return;
      node.disabled = !!busy;
    });
    if (!busy) return;
    var said = $('[data-progress-text]');
    var text = tr(busy.kind === 'add' ? 'adopt.checking' : 'adopt.progress', { host: busy.host });
    if (said.textContent !== text) said.textContent = text;
    $('[data-progress-time]').textContent = tr('adopt.elapsed', { s: Math.floor((Date.now() - busy.since) / 1000) });
  }

  function take(body, scope) {
    if (state.busy) return Promise.resolve(false);
    startBusy(body.key ? 'add' : 'adopt', body.host);
    var path = scope.getAttribute('data-form') === 'manual' ? 'api/devices' : 'api/devices/adopt';
    var target = statusOf(scope);
    say(target, null, null);
    return request('POST', path, body, TAKE_MS).then(function (data) {
      endBusy();
      if (state.screen !== 'main') return false;
      setDevices(data.devices);
      say(target, 'ok', key('adopt.done', { name: data.name }));
      return true;
    }, function (f) {
      endBusy();
      if (f.code === 'foreign_host') showHost();
      else if (f.code === 'login_required') {
        if (state.screen === 'main') expire();
      } else if (state.screen === 'main') {
        if (f.code === 'timeout') say(target, 'error', key('adopt.slow'));
        else showFailure(scope, f);
      }
      return false;
    }).then(function (ok) {
      refreshDevices();
      refreshFound();
      return ok;
    });
  }

  function adoptFound(found) {
    var card = $('[data-status="found"]').closest('.card');
    take({ host: found.host, port: found.port }, card);
  }

  function submitManual(form) {
    clearErrors(form);
    var address = addressFrom(form, fieldInput(form, 'host'), fieldInput(form, 'port'));
    if (!address) return;
    var secret = fieldInput(form, 'key').value.trim();
    var body = { host: address.host, port: address.port };
    if (secret) body.key = secret;
    take(body, form).then(function (ok) {
      if (ok) form.reset();
    });
  }

  function confirmAction(describe, okKey) {
    var dialog = $('[data-confirm]');
    if (!dialog || typeof dialog.showModal !== 'function') {
      return Promise.resolve(window.confirm(describe(state.lang)));
    }
    $('[data-confirm-text]').textContent = describe(state.lang);
    var ok = $('[data-confirm-ok]');
    var cancel = $('[data-confirm-cancel]');
    ok.textContent = tr(okKey);
    return new Promise(function (resolve) {
      function finish(answer) {
        ok.onclick = null;
        cancel.onclick = null;
        dialog.oncancel = null;
        if (dialog.open) dialog.close();
        resolve(answer);
      }
      ok.onclick = function () {
        finish(true);
      };
      cancel.onclick = function () {
        finish(false);
      };
      dialog.oncancel = function (event) {
        event.preventDefault();
        finish(false);
      };
      dialog.showModal();
      cancel.focus();
    });
  }

  function buildTimingFields() {
    TIMING_FIELDS.forEach(function (field) {
      var id = 'timing-' + field.name;
      var hintId = id + '-hint';
      var decimal = field.kind !== 'count';
      var input = el('input', {
        id: id,
        type: 'text',
        inputmode: decimal ? 'decimal' : 'numeric',
        'data-field': 'timing.' + field.name,
        'data-decimal': decimal,
        autocomplete: 'off',
        'aria-describedby': hintId
      });
      var parts = [input];
      if (decimal) parts.push(el('span', { 'class': 'unit', i18n: 'unit.s' }));
      var box = el('div', { 'class': 'field' }, [
        el('label', { 'for': id, i18n: 'timing.' + field.name + '.label' }),
        el('div', { 'class': 'with-unit' }, parts),
        el('p', { 'class': 'hint', id: hintId, 'data-timing-hint': field.name })
      ]);
      $(field.group === 'main' ? '[data-timing-main]' : '[data-timing-more]').appendChild(box);
    });
  }

  function renderHints() {
    TIMING_FIELDS.forEach(function (field) {
      var node = $('[data-timing-hint="' + field.name + '"]');
      if (!node) return;
      node.textContent = tr('timing.' + field.name + '.hint') + ' ' +
        tr('timing.default', { v: formatSetting(field.kind, DEFAULTS.timing[field.name], state.lang) });
    });
    var vars = {
      'openai.vad_hint': { v: formatNumber(DEFAULTS.openai.vad_threshold, state.lang) },
      'openai.silence_hint': { v: tr('unit.millis', { n: DEFAULTS.openai.silence_ms }) },
      'server.port_hint': { v: DEFAULTS.http_port }
    };
    $$('[data-hint]').forEach(function (node) {
      var name = node.getAttribute('data-hint');
      node.textContent = tr(name, vars[name]);
    });
  }

  function reformatNumbers() {
    $$('input[data-decimal]').forEach(function (input) {
      var n = parseNumber(input.value);
      if (!isNaN(n) && input.getAttribute('aria-invalid') !== 'true') input.value = formatNumber(n, state.lang);
    });
  }

  function markLang() {
    $$('[data-set-lang]').forEach(function (button) {
      button.setAttribute('aria-pressed', button.getAttribute('data-set-lang') === state.lang ? 'true' : 'false');
    });
  }

  function markTheme() {
    $$('[data-theme-toggle]').forEach(function (button) {
      button.setAttribute('aria-pressed', state.theme === 'light' ? 'true' : 'false');
    });
  }

  function markUrl(lang) {
    var history = window.history;
    if (!window.location || !history || typeof history.replaceState !== 'function') return;
    try {
      history.replaceState(history.state, '', urlWithLang(window.location.href, lang));
    } catch (e) {}
  }

  function setLang(lang) {
    if (LANGS.indexOf(lang) < 0) return;
    state.lang = lang;
    writeStored(LANG_KEY, lang);
    markUrl(lang);
    doc.documentElement.setAttribute('lang', lang);
    applyText(doc);
    markLang();
    renderHints();
    reformatNumbers();
    renderSettingsState();
    $$('[data-say]').forEach(renderSaid);
    cards.forEach(function (card) {
      if (card.device) updateSpeaker(card, card.device);
    });
    renderFound(true);
    if (state.busy) renderBusy();
  }

  function setTheme(theme) {
    if (THEMES.indexOf(theme) < 0) return;
    state.theme = theme;
    writeStored(THEME_KEY, theme);
    doc.documentElement.setAttribute('data-theme', theme);
    markTheme();
  }

  function onSubmit(event) {
    var form = event.target;
    var name = form.getAttribute && form.getAttribute('data-form');
    if (!name) return;
    event.preventDefault();
    if (name === 'setup') submitSetup(form);
    else if (name === 'login') submitLogin(form);
    else if (name === 'password') submitPassword(form);
    else if (name === 'manual') submitManual(form);
    else submitSection(name, form);
  }

  function onClick(event) {
    var target = event.target.closest && event.target.closest('[data-action], [data-set-lang], [data-theme-toggle]');
    if (!target) return;
    if (target.hasAttribute('data-set-lang')) return setLang(target.getAttribute('data-set-lang'));
    if (target.hasAttribute('data-theme-toggle')) return setTheme(state.theme === 'light' ? 'dark' : 'light');
    var action = target.getAttribute('data-action');
    if (action === 'retry') boot();
    else if (action === 'logout') logout();
    else if (action === 'clear-key') clearKey();
    else if (action === 'reset-instructions') resetInstructions();
    else if (action === 'timing-defaults') fillTimingDefaults();
    else if (action === 'reload-settings') loadSettings();
    else if (action === 'goto-key') {
      event.preventDefault();
      openTab('conversation');
      $('#openai-key').focus();
    }
  }

  function onInput(event) {
    var input = event.target;
    if (input.getAttribute && input.getAttribute('aria-invalid') === 'true') clearField(input);
    if (input.id === 'assistant-language') updateLanguageNote();
    if (input.closest && input.closest('[data-form]')) markUnsaved();
  }

  function onLeave(event) {
    if (state.screen !== 'main' || !unsavedForms().length) return;
    event.preventDefault();
    event.returnValue = true;
  }

  function poll() {
    if (state.screen !== 'main' || doc.visibilityState === 'hidden') return;
    if (!state.settings) loadSettings();
    var offline = !$('[data-banner="offline"]').hidden;
    if (state.tab !== 'speakers' && !offline) return;
    refreshDevices();
    if (state.tab === 'speakers') refreshFound();
  }

  function start() {
    buildTimingFields();
    fieldInput($('[data-form="manual"]'), 'port').setAttribute('value', String(DEFAULTS.port));
    applyText(doc);
    markLang();
    markUrl(state.lang);
    markTheme();
    renderHints();
    lockUnloaded();
    doc.addEventListener('submit', onSubmit);
    doc.addEventListener('click', onClick);
    doc.addEventListener('input', onInput);
    doc.addEventListener('change', onInput);
    doc.addEventListener('visibilitychange', poll);
    window.addEventListener('beforeunload', onLeave);
    window.addEventListener('hashchange', function () {
      if (state.screen !== 'main') return;
      selectTab(tabFromHash());
      window.scrollTo(0, 0);
    });
    window.setInterval(poll, POLL_MS);
    boot();
  }

  if (doc.readyState === 'loading') doc.addEventListener('DOMContentLoaded', start);
  else start();
})(window);
