(function (window) {
  'use strict';

  var H = (window.PYTAICHENKO_PAGE = window.PYTAICHENKO_PAGE || {});
  var doc = window.document;
  var LANGS = ['uk', 'en'];
  var THEMES = ['dark', 'light'];
  var LANG_KEY = 'pytaichenko-lang';
  var THEME_KEY = 'pytaichenko-theme';
  var TABS = ['conversation', 'sound', 'timing', 'log', 'about', 'access'];
  var POLL_MS = 5000;
  var LOG_MS = 2000;
  var LOG_MAX = 400;
  var REQUEST_MS = 20000;
  var CHECK_MS = 30000;
  var INSTALL_MS = 300000;
  var UPLOAD_MS = 300000;
  var UPLOAD_MAX = 4 * 1024 * 1024;
  var RESTART_FIRST_MS = 3000;
  var RESTART_POLL_MS = 2500;
  var RESTART_REQUEST_MS = 4000;
  var RESTART_TRIES = 72;
  var RESTART_SURE_TRIES = 6;
  var MIN_PASSWORD = 8;
  var MAX_PASSWORD_BYTES = 256;
  var MIN_STOP_GRACE = 0.5;
  var EQ_HZ = [250, 600, 1500, 3500, 8000];

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
    sound: { mic_gain_db: 12, low_cut_hz: 200, loudness_gain_db: 6, eq: 0 }
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

  var SOUND_FIELDS = [
    { name: 'mic_gain_db', kind: 'gain', unit: 'unit.db' },
    { name: 'low_cut_hz', kind: 'hz', unit: 'unit.hz' },
    { name: 'loudness_gain_db', kind: 'loud', unit: 'unit.db' }
  ];

  var NUMBER_RANGES = {
    gain: [0, 30],
    hz: [80, 400],
    loud: [0, 12],
    eq: [-12, 12]
  };

  function eqName(i) {
    return 'eq' + i;
  }

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
    sound: SOUND_FIELDS.map(function (f) {
      return { name: f.name, kind: f.kind };
    }).concat(EQ_HZ.map(function (hz, i) {
      return { name: eqName(i), kind: 'eq', group: 'eq_db', index: i };
    })),
    wake: [
      { name: 'wake_word', kind: 'choice' }
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
    'sound.wake_word': 'wake.title',
    'sound.mic_gain_db': 'sound.mic_gain_db.label',
    'sound.low_cut_hz': 'sound.low_cut_hz.label',
    'sound.loudness_gain_db': 'sound.loudness_gain_db.label',
    'sound.eq_db': 'sound.eq.title',
    'sound.volume': 'volume.label',
    volume: 'volume.label',
    password: 'login.password',
    current: 'password.current',
    new: 'password.new',
    repeat: 'password.repeat'
  };

  var FIELD_ALIASES = { 'sound.eq_db': 'sound.eq0' };

  var CONTEXT_CODES = {
    password: { wrong_password: 'wrong_current' },
    update: { too_large: 'update_too_large' }
  };
  var DETAILED = { save_failed: true, update_busy: true };

  var RULES = [
    [/^invalid value$/, 'rule.invalid_value'],
    [/^must be a number from 0 to 1$/, 'rule.unit'],
    [/^must be one of (.+)$/, 'rule.one_of', ['list']],
    [/^unknown (setting|field)$/, 'rule.unknown'],
    [/^must be (\d+) to (\d+) characters$/, 'rule.length', ['low', 'high']],
    [/^must be text of at most (\d+) bytes$/, 'rule.text_bytes', ['n']]
  ];

  var KIND_RULES = {
    seconds: { key: 'rule.above_zero' },
    seconds0: { key: 'rule.zero_or_more' },
    grace: { key: 'rule.grace' },
    count: { key: 'rule.int_min', vars: { low: 0 } },
    ms: { key: 'rule.int_min', vars: { low: 1 } },
    unit: { key: 'rule.unit' },
    gain: { key: 'rule.number_range', vars: { low: 0, high: 30 } },
    hz: { key: 'rule.number_range', vars: { low: 80, high: 400 } },
    loud: { key: 'rule.number_range', vars: { low: 0, high: 12 } },
    eq: { key: 'rule.number_range', vars: { low: -12, high: 12 } }
  };

  var TEXT = {
    uk: {
      'meta.title': 'Питайченко — налаштування',
      'header.name': 'Питайченко',
      'header.sub': 'налаштування',
      'header.theme_label': 'Світла тема',
      'header.lang_label': 'Мова сторінки',
      'header.logout': 'Вийти',
      'header.donate': 'Donate',
      loading: 'Завантаження…',
      retry: 'Спробувати ще раз',
      cancel: 'Скасувати',
      save: 'Зберегти',
      saving: 'Зберігаю…',
      saved: 'Збережено. Діє з наступного питання.',
      saved_plain: 'Збережено.',
      nothing: 'Нічого не змінено.',
      more: 'Тонкі налаштування',
      'form.check': 'Перевірте позначене поле.',
      'form.unsaved': 'Є незбережені зміни',
      'form.defaults': 'Типові значення',
      'form.defaults_done': 'Підставлено типові значення. Натисніть «Зберегти», щоб застосувати.',
      'form.default': 'Типово: {v}.',
      'unit.s': 'с',
      'unit.ms': 'мс',
      'unit.db': 'дБ',
      'unit.hz': 'Гц',
      'unit.seconds': '{n} с',
      'unit.millis': '{n} мс',
      'unit.decibels': '{n} дБ',
      'unit.hertz': '{n} Гц',
      'unit.minutes': '{n} хв',
      'unit.hours': '{n} год',
      'lang.uk': 'українська',
      'lang.en': 'англійська',
      'lang.es': 'іспанська',
      'lang.fr': 'французька',
      'lang.de': 'німецька',
      'lang.it': 'італійська',
      'lang.pt': 'португальська',
      'lang.pl': 'польська',
      'lang.nl': 'нідерландська',
      'lang.tr': 'турецька',
      'offline.title': 'Колонка не відповідає',
      'offline.text': "Сторінка не може зв'язатися з колонкою. Перевірте, що колонка ввімкнена й підключена до Wi-Fi, і спробуйте ще раз.",
      'host.title': 'Колонка не приймає цю адресу',
      'host.opened': 'Сторінку відкрито за адресою {host}.',
      'host.why': "Щоб ніхто чужий не дістався налаштувань через підставне ім'я з інтернету, колонка відкриває їх лише за домашньою адресою:",
      'host.ip': 'IP-адреса колонки, наприклад http://192.0.2.10',
      'host.name': "ім'я колонки з кінцівкою .local, наприклад http://pytaichenko-a1b2c3.local",
      'setup.title': 'Створіть пароль',
      'setup.lead': 'Пароль захищає налаштування: ключ OpenAI й інструкції. Його знатимете лише ви.',
      'setup.language': 'Асистент розмовлятиме мовою цієї сторінки — українською. Іншу мову можна обрати потім у розділі «Розмова».',
      'setup.password': 'Пароль',
      'setup.repeat': 'Повторіть пароль',
      'setup.submit': 'Створити пароль',
      'login.title': 'Вхід',
      'login.password': 'Пароль',
      'login.submit': 'Увійти',
      'login.checking': 'Перевіряю…',
      'login.empty': 'Введіть пароль.',
      'login.expired': 'Вхід закінчився (наприклад, колонку перезапустили). Увійдіть знову.',
      'login.loggedout': 'Ви вийшли. Колонка працює далі.',
      'password.rule': 'Щонайменше 8 символів.',
      'password.forgot': "Якщо забудете пароль, утримуйте кнопку зверху на колонці близько 10 секунд, доки кільце не блимне п'ять разів: пароль скинеться, а Wi-Fi, ключ і налаштування лишаться. Якщо це не допоможе, колонку доведеться прошити знову зі стиранням: тоді скинуться всі налаштування й пароль.",
      'password.title': 'Пароль сторінки',
      'password.current': 'Поточний пароль',
      'password.new': 'Новий пароль',
      'password.repeat': 'Повторіть новий пароль',
      'password.hint': 'Щонайменше 8 символів. Після зміни вхід на інших пристроях закриється.',
      'password.submit': 'Змінити пароль',
      'password.changed': 'Пароль змінено. На інших пристроях доведеться увійти знову.',
      'password.mismatch': 'Паролі не збігаються.',
      'password.short': 'Закороткий пароль: потрібно щонайменше 8 символів.',
      'password.long': 'Задовгий пароль: він не може займати більше 256 байтів (українська літера займає 2).',
      'password.empty': 'Введіть поточний пароль.',
      'nav.label': 'Розділи',
      'nav.conversation': 'Розмова',
      'nav.sound': 'Звук',
      'nav.timing': 'Часи',
      'nav.log': 'Журнал',
      'nav.about': 'Про колонку',
      'nav.access': 'Доступ',
      'banner.nokey': 'Ключ OpenAI ще не задано, тож колонка не зможе відповідати.',
      'banner.nokey_link': 'Вписати ключ',
      'banner.offline': "Немає зв'язку з колонкою. Сторінка пробує знову сама.",
      'banner.settings': 'Не вдалося завантажити налаштування з колонки. Сторінка пробує знову сама.',
      'settings.not_loaded': 'Налаштування ще не завантажились з колонки, тож зберегти нічого не можна. Зачекайте трохи й спробуйте ще раз.',
      'conversation.title': 'Розмова',
      'conversation.applies': "Зміни діють з наступного питання. Якщо розмова вже йде, асистент почне її наново й не пам'ятатиме попередніх питань.",
      'openai.title': 'OpenAI',
      'openai.key': 'Ключ OpenAI',
      'openai.key_set': 'задано ✓',
      'openai.key_unset': 'не задано',
      'openai.key_placeholder': 'sk-…',
      'openai.key_placeholder_set': 'вставте новий ключ, щоб замінити',
      'openai.key_hint': 'Ключ створюють на сайті platform.openai.com. Розмови оплачуються за тарифами OpenAI, тож краще завести для колонки окремий проєкт з обмеженням витрат. Ключ зберігається лише в цій колонці й назад не показується.',
      'openai.key_clear': 'Прибрати ключ',
      'openai.key_clear_ok': 'Прибрати ключ',
      'openai.key_clear_confirm': 'Прибрати ключ OpenAI з колонки? Вона не зможе відповідати, доки ви не впишете новий.',
      'openai.key_cleared': 'Ключ прибрано.',
      'openai.check': "Перевірити з'єднання з OpenAI",
      'openai.check_hint': "Колонка відкриє з'єднання з OpenAI за збереженими ключем і моделлю й одразу закриє його. Якщо щойно змінили поля, спершу натисніть «Зберегти». Перевірка триває кілька секунд.",
      'openai.check_wait': "Перевіряю з'єднання…",
      'openai.check_ok': "З'єднання працює. Модель {model}, голос {voice}.",
      'openai.model': 'Модель',
      'openai.model_hint': 'gpt-realtime-2.1-mini — швидша й дешевша; gpt-realtime-2.1 — точніша, але дорожча; gpt-live-1 — найживіша розмова, $0,05 за хвилину розмови плюс невелика плата за модель, що виконує дії (гучність, завершення).',
      'openai.live_note': 'Для gpt-live-1 поріг голосу й пауза не діють: модель сама чує, коли ви закінчили.',
      'openai.voice': 'Голос',
      'openai.voice_hint': 'Голос, яким відповідає асистент. Найприродніше звучать marin і cedar.',
      'openai.reasoning': 'Обмірковування відповіді',
      'openai.reasoning_none': 'не вказувати',
      'openai.reasoning_hint': 'minimal — найшвидші відповіді. Що вище, то довше асистент думає перед відповіддю.',
      'openai.log_heard': 'Записувати в журнал, що почув асистент',
      'openai.log_heard_hint': "Почутий текст з'являтиметься в розділі «Журнал». Це допомагає зрозуміти, чому асистент відповів не те. Трохи збільшує витрати на OpenAI.",
      'openai.vad': 'Чутливість до голосу',
      'openai.vad_hint': 'Число від 0 до 1: наскільки чітким має бути звук, щоб вважатися мовою. Більше число — менше спрацювань від шуму, але говорити треба виразніше. Типово: {v}.',
      'openai.silence': 'Тиша, що завершує питання',
      'openai.silence_hint': 'Скільки мілісекунд тиші означає, що ви договорили. Типово: {v}.',
      'field.log_heard': 'Журнал почутого',
      'field.message': '«{field}»: {rule}.',
      'assistant.title': 'Асистент',
      'assistant.language': 'Мова розмови',
      'assistant.language_hint': 'Цією мовою асистент слухає й відповідає. Спершу це мова сторінки, якою створювали пароль; перемикач мови сторінки її не змінює. Для інших мов, крім української й англійської, типові інструкції написано англійською, а відповідає асистент вибраною мовою.',
      'assistant.language_note': 'Інструкції нижче замінено типовими для нової мови: для української — українськими, для інших мов — англійськими. Збережіть, щоб застосувати. Якщо повернути попередню мову, повернеться й попередній текст.',
      'assistant.instructions': 'Інструкції',
      'assistant.instructions_hint': "Як асистентові поводитися й відповідати. Тут можна, наприклад, дати йому ім'я.",
      'assistant.reset': 'Повернути типові',
      'assistant.reset_ok': 'Повернути типові',
      'assistant.reset_confirm': 'Замінити інструкції типовим текстом ({lang})? Ваш власний текст буде втрачено.',
      'assistant.reset_done': 'Повернуто типові інструкції ({lang}).',
      'assistant.language_switched': 'Збережено. Мову розмови змінено, діє з наступного питання.',
      'sound.title': 'Звук',
      'volume.title': 'Гучність',
      'volume.label': 'Гучність відповіді',
      'volume.value': '{n} %',
      'volume.hint': 'Голосом теж можна: «тихіше», «гучніше».',
      'volume.test': 'Тест звуку',
      'volume.test_ok': 'Грає тестовий звук: чотири ноти вгору, з гучністю як на повзунку.',
      'volume.test_busy': 'Колонка зараз розмовляє. Спробуйте, коли вона замовкне.',
      'wake.title': 'Ключова фраза',
      'wake.label': 'Фраза, після якої колонка слухає',
      'wake.hint': 'Скажіть цю фразу, і колонка почне слухати. Оберіть ту, яку вам зручніше казати.',
      'sound.card': 'Мікрофон і відповідь',
      'sound.lead': 'Тонке налаштування звуку. Якщо все звучить добре, нічого міняти не треба. Дробові числа можна писати з комою.',
      'sound.mic_gain_db.label': 'Підсилення мікрофона',
      'sound.mic_gain_db.hint': 'Наскільки підсилюється голос з мікрофона перед відправкою. Більше — краще чути тихий голос, але й шум теж.',
      'sound.low_cut_hz.label': 'Прибрати низький гул',
      'sound.low_cut_hz.hint': 'Звуки нижче цієї частоти вирізаються з мікрофона. Допомагає, якщо чути гул чи удари.',
      'sound.loudness_gain_db.label': 'Підсилення відповіді',
      'sound.loudness_gain_db.hint': 'На скільки відповідь робиться гучнішою за початкову. Забагато — звук може хрипіти.',
      'sound.eq.title': 'Еквалайзер відповіді',
      'sound.eq.hint': 'Підсилює або приглушує частини звуку: низькі зліва, високі справа. Значення від -12 до 12 дБ, 0 — без змін.',
      'log.title': 'Журнал',
      'log.lead': 'Останні події колонки: що вона почула, помилки, перезапуски. Журнал оновлюється сам, поки цей розділ відкритий, і не зберігається після вимкнення колонки.',
      'log.empty': 'Журнал поки порожній.',
      'about.title': 'Про колонку',
      'about.name': "Ім'я",
      'about.version': 'Версія',
      'about.ip': 'IP-адреса',
      'about.signal': 'Сигнал Wi-Fi',
      'about.signal_value': '{q}, {n} дБм',
      'about.q_great': 'відмінний',
      'about.q_good': 'добрий',
      'about.q_weak': 'слабкий',
      'about.q_bad': 'дуже слабкий',
      'update.title': 'Оновлення',
      'update.current': 'Встановлена версія',
      'update.latest': 'Остання версія',
      'update.hint': 'Колонка заглядає на GitHub лише тоді, коли ви натискаєте «Перевірити оновлення». Під час оновлення вона завантажує нову прошивку, перезапускається й за кілька хвилин працює знову; налаштування й пароль лишаються.',
      'update.check': 'Перевірити оновлення',
      'update.check_wait': 'Дивлюся, чи є нова версія…',
      'update.available': 'Є нова версія: {version}.',
      'update.up_to_date': 'У вас остання версія.',
      'update.none': 'Нових версій поки немає.',
      'update.install': 'Оновити до {version}',
      'update.confirm': 'Оновити колонку до версії {version}? Вона завантажить нову прошивку й перезапуститься. Це може тривати кілька хвилин, колонку не вимикайте.',
      'update.confirm_ok': 'Оновити',
      'update.installing': 'Колонка завантажує нову версію. Це може тривати кілька хвилин, не вимикайте її.',
      'update.restarting': 'Колонка перезапускається. Сторінка оновиться сама, коли колонка буде готова.',
      'update.back': 'Колонка знову працює. Оновлюю сторінку…',
      'update.restart_timeout': 'Колонка довго не відповідає після оновлення. Зачекайте ще трохи й оновіть сторінку. Якщо нова версія не запуститься, колонка сама повернеться до попередньої.',
      'update.file': 'Прошивка з файлу',
      'update.file_hint': 'Для випадків, коли колонка не має доступу до GitHub, або для власної збірки. Потрібен файл pytaichenko-….ota.bin версії 2.',
      'update.upload': 'Завантажити файл',
      'update.no_file': 'Спершу виберіть файл прошивки.',
      'update.file_too_big': 'Цей файл завеликий для прошивки: понад 4 МБ.',
      'update.file_confirm': 'Завантажити «{name}» і оновити колонку з цього файлу? Колонка перезапуститься, не вимикайте її.',
      'update.uploading': 'Надсилаю файл: {n}%',
      'update.upload_done': 'Файл надіслано. Колонка перевіряє його й записує…',
      'timing.title': 'Часи',
      'timing.lead': 'Скільки колонка чекає й слухає. Частки секунди можна писати з комою, наприклад 0,4.',
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
      'timing.ready_check_s.hint': "Перед новим питанням колонка перевіряє, чи ще живе відкрите з'єднання, і чекає на відгук стільки секунд. Не дочекавшись, відкриває нове.",
      'access.title': 'Доступ',
      'logout.title': 'Вийти',
      'logout.text': 'Вихід лише на цьому пристрої. Колонка працюватиме й далі.',
      'rule.empty': 'значення не може бути порожнім',
      'rule.int_min': 'потрібне ціле число, не менше за {low}',
      'rule.unit': 'потрібне число від 0 до 1',
      'rule.one_of': 'потрібне одне зі значень: {list}',
      'rule.zero_or_more': 'потрібне число секунд, 0 або більше',
      'rule.grace': 'потрібне число секунд, 0,5 або більше',
      'rule.above_zero': 'потрібне число секунд, більше за 0',
      'rule.number_range': 'потрібне число від {low} до {high}',
      'rule.invalid_value': 'колонка не прийняла це значення',
      'rule.unknown': 'колонка такого не знає; мабуть, сторінка старіша за прошивку, тож оновіть її',
      'rule.length': 'потрібно від {low} до {high} символів',
      'rule.text_bytes': 'потрібен текст не довший за {n} байтів',
      'rule.other': 'колонка не прийняла значення',
      'err.network': "Немає зв'язку з колонкою. Перевірте, що вона ввімкнена, і спробуйте ще раз.",
      'err.timeout': 'Колонка довго не відповідає. Спробуйте ще раз трохи згодом.',
      'err.foreign_host': "Колонка не приймає адресу, за якою відкрито сторінку. Відкрийте її за IP-адресою колонки або за ім'ям з кінцівкою .local.",
      'err.foreign_origin': 'Колонка відхилила запит, бо той прийшов не з цієї сторінки. Відкрийте налаштування напряму за адресою колонки.',
      'err.login_required': 'Вхід закінчився. Увійдіть знову, будь ласка.',
      'err.json_required': 'Колонка не зрозуміла запит сторінки. Оновіть сторінку; якщо не допоможе, сторінка й прошивка різних версій.',
      'err.bad_json': 'Колонка не зрозуміла запит сторінки. Оновіть сторінку; якщо не допоможе, сторінка й прошивка різних версій.',
      'err.method_not_allowed': 'Колонка не зрозуміла запит сторінки. Оновіть сторінку; якщо не допоможе, сторінка й прошивка різних версій.',
      'err.not_found': 'Колонка не знає такого запиту. Оновіть сторінку; якщо не допоможе, сторінка й прошивка різних версій.',
      'err.too_large': 'Задовгий текст: колонка стільки не приймає. Скоротіть його.',
      'err.internal': 'У колонці щось пішло не так. Подробиці — в розділі «Журнал».',
      'err.http': 'Колонка відповіла незнайомою помилкою. Спробуйте оновити сторінку.',
      'err.save_failed': "Колонка не змогла записати налаштування у свою пам'ять. Спробуйте ще раз; якщо повторюється, перезапустіть колонку.",
      'err.password_set': 'Пароль уже створено. Увійдіть із ним.',
      'err.no_password': 'Пароль ще не створено. Спершу створіть його.',
      'err.bad_password': 'Пароль має містити від 8 до 256 символів.',
      'err.wrong_password': 'Неправильний пароль. Спробуйте ще раз.',
      'err.wrong_current': 'Поточний пароль неправильний. Спробуйте ще раз.',
      'err.slow_down': 'Забагато перевірок пароля з цієї адреси одночасно. Зачекайте кілька секунд і спробуйте знову.',
      'err.invalid': 'Колонка не прийняла одне зі значень. Перевірте поля форми.',
      'err.openai_bad_key': 'OpenAI не прийняв ключ. Перевірте, що ключ введено повністю й що він ще діє.',
      'err.openai_unreachable': "Колонка не змогла зв'язатися з OpenAI. Перевірте інтернет і спробуйте ще раз.",
      'err.openai_timeout': 'OpenAI не відповів вчасно. Спробуйте ще раз трохи згодом.',
      'err.openai_rejected': 'OpenAI відповів помилкою на запит. Перевірте назву моделі на цій вкладці й спробуйте ще раз.',
      'err.openai_no_key': 'Ключ OpenAI ще не збережено. Впишіть його вище й натисніть «Зберегти».',
      'err.busy': "Колонка зараз розмовляє або вже перевіряє з'єднання. Спробуйте за хвилину.",
      'err.update_not_ours': 'Цей файл не підходить: це не прошивка Питайченка версії 2. Нічого не змінено.',
      'err.update_failed': 'Оновлення не вдалося. Колонка лишилась на попередній версії; подробиці в розділі «Журнал».',
      'err.update_busy': 'Колонка зараз зайнята: розмовляє, перевіряє нову прошивку або вже оновлюється. Спробуйте за хвилину.',
      'err.update_too_large': 'Цей файл завеликий для прошивки: понад 4 МБ.',
      'err.no_release': 'Нової версії для встановлення не знайдено.'
    },
    en: {
      'meta.title': 'Pytaichenko — settings',
      'header.name': 'Pytaichenko',
      'header.sub': 'settings',
      'header.theme_label': 'Light theme',
      'header.lang_label': 'Page language',
      'header.logout': 'Log out',
      'header.donate': 'Donate',
      loading: 'Loading…',
      retry: 'Try again',
      cancel: 'Cancel',
      save: 'Save',
      saving: 'Saving…',
      saved: 'Saved. Applies from the next question.',
      saved_plain: 'Saved.',
      nothing: 'Nothing has changed.',
      more: 'Fine-tuning',
      'form.check': 'Please check the marked field.',
      'form.unsaved': 'Unsaved changes',
      'form.defaults': 'Default values',
      'form.defaults_done': 'Default values filled in. Press “Save” to apply them.',
      'form.default': 'Default: {v}.',
      'unit.s': 's',
      'unit.ms': 'ms',
      'unit.db': 'dB',
      'unit.hz': 'Hz',
      'unit.seconds': '{n} s',
      'unit.millis': '{n} ms',
      'unit.decibels': '{n} dB',
      'unit.hertz': '{n} Hz',
      'unit.minutes': '{n} min',
      'unit.hours': '{n} h',
      'lang.uk': 'Ukrainian',
      'lang.en': 'English',
      'lang.es': 'Spanish',
      'lang.fr': 'French',
      'lang.de': 'German',
      'lang.it': 'Italian',
      'lang.pt': 'Portuguese',
      'lang.pl': 'Polish',
      'lang.nl': 'Dutch',
      'lang.tr': 'Turkish',
      'offline.title': 'The speaker is not answering',
      'offline.text': 'The page cannot reach the speaker. Check that it is on and connected to Wi-Fi, then try again.',
      'host.title': 'The speaker does not accept this address',
      'host.opened': 'This page was opened at {host}.',
      'host.why': 'So that nobody can reach the settings through a fake name from the internet, the speaker opens them only at a home address:',
      'host.ip': 'the speaker IP address, for example http://192.0.2.10',
      'host.name': 'the speaker name ending in .local, for example http://pytaichenko-a1b2c3.local',
      'setup.title': 'Create a password',
      'setup.lead': 'The password protects the settings: the OpenAI key and the instructions. Only you will know it.',
      'setup.language': 'The assistant will talk in the language of this page, English. You can choose another one later in the “Conversation” section.',
      'setup.password': 'Password',
      'setup.repeat': 'Repeat the password',
      'setup.submit': 'Create password',
      'login.title': 'Log in',
      'login.password': 'Password',
      'login.submit': 'Log in',
      'login.checking': 'Checking…',
      'login.empty': 'Enter the password.',
      'login.expired': 'You have been logged out (for example, the speaker restarted). Please log in again.',
      'login.loggedout': 'You have logged out. The speaker keeps working.',
      'password.rule': 'At least 8 characters.',
      'password.forgot': 'If you forget the password, hold the top button of the speaker for about 10 seconds, until the ring flashes five times: the password is reset, and Wi-Fi, the key and the settings stay. If that does not help, the speaker has to be flashed again with erase: then all settings and the password are reset.',
      'password.title': 'Page password',
      'password.current': 'Current password',
      'password.new': 'New password',
      'password.repeat': 'Repeat the new password',
      'password.hint': 'At least 8 characters. After the change, other devices are logged out.',
      'password.submit': 'Change password',
      'password.changed': 'Password changed. Other devices will need to log in again.',
      'password.mismatch': 'The passwords do not match.',
      'password.short': 'Too short: use at least 8 characters.',
      'password.long': 'Too long: the password cannot take more than 256 bytes (a Ukrainian letter takes 2).',
      'password.empty': 'Enter the current password.',
      'nav.label': 'Sections',
      'nav.conversation': 'Conversation',
      'nav.sound': 'Sound',
      'nav.timing': 'Timing',
      'nav.log': 'Log',
      'nav.about': 'About',
      'nav.access': 'Access',
      'banner.nokey': 'The OpenAI key is not set yet, so the speaker cannot answer.',
      'banner.nokey_link': 'Enter the key',
      'banner.offline': 'No connection to the speaker. The page keeps trying by itself.',
      'banner.settings': 'Could not load the settings from the speaker. The page keeps trying by itself.',
      'settings.not_loaded': 'The settings have not loaded from the speaker yet, so nothing can be saved. Wait a moment and try again.',
      'conversation.title': 'Conversation',
      'conversation.applies': 'Changes apply from the next question. A conversation already going on starts afresh and forgets the earlier questions.',
      'openai.title': 'OpenAI',
      'openai.key': 'OpenAI key',
      'openai.key_set': 'set ✓',
      'openai.key_unset': 'not set',
      'openai.key_placeholder': 'sk-…',
      'openai.key_placeholder_set': 'paste a new key to replace it',
      'openai.key_hint': 'You create a key at platform.openai.com. Conversations are paid at OpenAI prices, so it is best to create a separate project for the speaker with a spending limit. The key is kept only in this speaker and is never shown back.',
      'openai.key_clear': 'Remove the key',
      'openai.key_clear_ok': 'Remove the key',
      'openai.key_clear_confirm': 'Remove the OpenAI key from the speaker? It cannot answer until you enter a new one.',
      'openai.key_cleared': 'The key has been removed.',
      'openai.check': 'Check the connection to OpenAI',
      'openai.check_hint': 'The speaker opens a connection to OpenAI with the saved key and model and closes it right away. If you just changed the fields, press Save first. The check takes a few seconds.',
      'openai.check_wait': 'Checking the connection…',
      'openai.check_ok': 'The connection works. Model {model}, voice {voice}.',
      'openai.model': 'Model',
      'openai.model_hint': 'gpt-realtime-2.1-mini is faster and cheaper; gpt-realtime-2.1 is more accurate but costs more; gpt-live-1 gives the most natural conversation, $0.05 per minute of conversation plus a small charge for the model that carries out actions (volume, ending).',
      'openai.live_note': 'With gpt-live-1 the voice threshold and pause do not apply: the model hears by itself when you have finished.',
      'openai.voice': 'Voice',
      'openai.voice_hint': 'The voice the assistant answers with. marin and cedar sound the most natural.',
      'openai.reasoning': 'Thinking before answering',
      'openai.reasoning_none': 'do not set',
      'openai.reasoning_hint': 'minimal gives the fastest answers. The higher it is, the longer the assistant thinks before answering.',
      'openai.log_heard': 'Write what the assistant heard to the log',
      'openai.log_heard_hint': 'The heard text shows up in the “Log” section. It helps to understand why the assistant answered something else. Costs a little more at OpenAI.',
      'openai.vad': 'Voice sensitivity',
      'openai.vad_hint': 'A number from 0 to 1: how clear a sound must be to count as speech. A higher number means fewer false starts from noise, but you need to speak more clearly. Default: {v}.',
      'openai.silence': 'Silence that ends a question',
      'openai.silence_hint': 'How many milliseconds of silence mean you have finished speaking. Default: {v}.',
      'field.log_heard': 'Log of what was heard',
      'field.message': '“{field}” {rule}.',
      'assistant.title': 'Assistant',
      'assistant.language': 'Conversation language',
      'assistant.language_hint': 'The assistant listens and answers in this language. At first it is the page language the password was created in; switching the page language does not change it. For languages other than Ukrainian and English the default instructions are in English, and the assistant answers in the chosen language.',
      'assistant.language_note': 'The instructions below are replaced with the defaults for the new language: Ukrainian ones for Ukrainian, English ones for every other language. Save to apply. Picking the previous language again brings the previous text back.',
      'assistant.instructions': 'Instructions',
      'assistant.instructions_hint': 'How the assistant should behave and answer. You can, for example, give it a name here.',
      'assistant.reset': 'Restore defaults',
      'assistant.reset_ok': 'Restore defaults',
      'assistant.reset_confirm': 'Replace the instructions with the default text ({lang})? Your own text will be lost.',
      'assistant.reset_done': 'Default instructions restored ({lang}).',
      'assistant.language_switched': 'Saved. The conversation language changed; it applies from the next question.',
      'sound.title': 'Sound',
      'volume.title': 'Volume',
      'volume.label': 'Answer volume',
      'volume.value': '{n} %',
      'volume.hint': 'You can also say “quieter” or “louder”.',
      'volume.test': 'Test sound',
      'volume.test_ok': 'Playing the test sound: four rising notes, at the volume on the slider.',
      'volume.test_busy': 'The speaker is talking right now. Try again when it is quiet.',
      'wake.title': 'Wake word',
      'wake.label': 'The phrase after which the speaker listens',
      'wake.hint': 'Say this phrase and the speaker starts listening. Choose the one that is easiest for you to say.',
      'sound.card': 'Microphone and answer',
      'sound.lead': 'Fine-tuning of the sound. If everything sounds fine, there is nothing to change. Fractions are written with a point, for example 0.5.',
      'sound.mic_gain_db.label': 'Microphone gain',
      'sound.mic_gain_db.hint': 'How much the voice from the microphone is boosted before it is sent. More makes a quiet voice easier to hear, but the noise too.',
      'sound.low_cut_hz.label': 'Remove low rumble',
      'sound.low_cut_hz.hint': 'Sounds below this frequency are cut out of the microphone signal. Helps if you hear a hum or thumps.',
      'sound.loudness_gain_db.label': 'Answer boost',
      'sound.loudness_gain_db.hint': 'How much louder the answer is made than the original. Too much and the sound may rasp.',
      'sound.eq.title': 'Answer equalizer',
      'sound.eq.hint': 'Boosts or softens parts of the sound: low on the left, high on the right. Values from -12 to 12 dB, 0 means no change.',
      'log.title': 'Log',
      'log.lead': 'The latest events of the speaker: what it heard, errors, restarts. The log refreshes by itself while this section is open and is not kept after the speaker is switched off.',
      'log.empty': 'The log is empty for now.',
      'about.title': 'About the speaker',
      'about.name': 'Name',
      'about.version': 'Version',
      'about.ip': 'IP address',
      'about.signal': 'Wi-Fi signal',
      'about.signal_value': '{q}, {n} dBm',
      'about.q_great': 'excellent',
      'about.q_good': 'good',
      'about.q_weak': 'weak',
      'about.q_bad': 'very weak',
      'update.title': 'Update',
      'update.current': 'Installed version',
      'update.latest': 'Latest version',
      'update.hint': 'The speaker looks at GitHub only when you press “Check for updates”. During an update it downloads the new firmware, restarts and works again in a few minutes; your settings and password stay.',
      'update.check': 'Check for updates',
      'update.check_wait': 'Looking for a new version…',
      'update.available': 'A new version is available: {version}.',
      'update.up_to_date': 'You have the latest version.',
      'update.none': 'There are no new versions yet.',
      'update.install': 'Update to {version}',
      'update.confirm': 'Update the speaker to version {version}? It will download the new firmware and restart. This can take a few minutes, so do not switch it off.',
      'update.confirm_ok': 'Update',
      'update.installing': 'The speaker is downloading the new version. This can take a few minutes, so do not switch it off.',
      'update.restarting': 'The speaker is restarting. The page will refresh by itself when the speaker is ready.',
      'update.back': 'The speaker works again. Refreshing the page…',
      'update.restart_timeout': 'The speaker has not answered for a long time after the update. Wait a little longer and refresh the page. If the new version fails to start, the speaker goes back to the previous one by itself.',
      'update.file': 'Firmware from a file',
      'update.file_hint': 'For when the speaker cannot reach GitHub, or for your own build. You need a pytaichenko-….ota.bin file of version 2.',
      'update.upload': 'Upload the file',
      'update.no_file': 'Choose a firmware file first.',
      'update.file_too_big': 'This file is too big for a firmware: over 4 MB.',
      'update.file_confirm': 'Upload “{name}” and update the speaker from this file? The speaker will restart, so do not switch it off.',
      'update.uploading': 'Sending the file: {n}%',
      'update.upload_done': 'The file is sent. The speaker is checking it and writing it…',
      'timing.title': 'Timing',
      'timing.lead': 'How long the speaker waits and listens. Fractions of a second are written with a point, for example 0.4.',
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
      'timing.ready_check_s.hint': 'Before a new question the speaker checks that the open connection is still alive and waits this long for a reply. Without one it opens a new connection.',
      'access.title': 'Access',
      'logout.title': 'Log out',
      'logout.text': 'Logs out on this device only. The speaker keeps working.',
      'rule.empty': 'cannot be empty',
      'rule.int_min': 'must be a whole number, {low} or more',
      'rule.unit': 'must be a number from 0 to 1',
      'rule.one_of': 'must be one of: {list}',
      'rule.zero_or_more': 'must be a number of seconds, 0 or more',
      'rule.grace': 'must be a number of seconds, 0.5 or more',
      'rule.above_zero': 'must be a number of seconds above 0',
      'rule.number_range': 'must be a number from {low} to {high}',
      'rule.invalid_value': 'was not accepted by the speaker',
      'rule.unknown': 'is unknown to the speaker; the page may be older than the firmware, so reload it',
      'rule.length': 'must be {low} to {high} characters long',
      'rule.text_bytes': 'must be text of at most {n} bytes',
      'rule.other': 'was not accepted by the speaker',
      'err.network': 'Cannot reach the speaker. Check that it is on and try again.',
      'err.timeout': 'The speaker is taking too long to answer. Try again a bit later.',
      'err.foreign_host': 'The speaker does not accept the address this page was opened at. Open it by the speaker IP address or by its name ending in .local.',
      'err.foreign_origin': 'The speaker refused the request because it did not come from this page. Open the settings directly at the speaker address.',
      'err.login_required': 'You have been logged out. Please log in again.',
      'err.json_required': 'The speaker did not understand the page request. Reload the page; if that does not help, the page and the firmware are different versions.',
      'err.bad_json': 'The speaker did not understand the page request. Reload the page; if that does not help, the page and the firmware are different versions.',
      'err.method_not_allowed': 'The speaker did not understand the page request. Reload the page; if that does not help, the page and the firmware are different versions.',
      'err.not_found': 'The speaker does not know this request. Reload the page; if that does not help, the page and the firmware are different versions.',
      'err.too_large': 'The text is too long for the speaker. Please shorten it.',
      'err.internal': 'Something went wrong in the speaker. The “Log” section has the details.',
      'err.http': 'The speaker answered with an unfamiliar error. Try reloading the page.',
      'err.save_failed': 'The speaker could not write the settings to its memory. Try again; if it keeps happening, restart the speaker.',
      'err.password_set': 'A password already exists. Log in with it.',
      'err.no_password': 'No password has been created yet. Create one first.',
      'err.bad_password': 'The password must be 8 to 256 characters long.',
      'err.wrong_password': 'Wrong password. Please try again.',
      'err.wrong_current': 'The current password is wrong. Please try again.',
      'err.slow_down': 'Too many password checks from this address at once. Wait a few seconds and try again.',
      'err.invalid': 'The speaker did not accept one of the values. Check the form fields.',
      'err.openai_bad_key': 'OpenAI did not accept the key. Check that it was entered in full and is still valid.',
      'err.openai_unreachable': 'The speaker could not reach OpenAI. Check the internet connection and try again.',
      'err.openai_timeout': 'OpenAI did not answer in time. Try again a little later.',
      'err.openai_rejected': 'OpenAI answered the request with an error. Check the model name on this tab and try again.',
      'err.openai_no_key': 'The OpenAI key is not saved yet. Enter it above and press Save.',
      'err.busy': 'The speaker is talking or already checking the connection. Try again in a minute.',
      'err.update_not_ours': 'This file does not fit: it is not a Pytaichenko version 2 firmware. Nothing was changed.',
      'err.update_failed': 'The update did not work. The speaker stays on the previous version; details are in the “Log” section.',
      'err.update_busy': 'The speaker is busy: it is talking, checking a new firmware or already updating. Try again in a minute.',
      'err.update_too_large': 'This file is too big for a firmware: over 4 MB.',
      'err.no_release': 'No new version to install was found.'
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
    if (kind === 'gain' || kind === 'loud' || kind === 'eq') {
      return t(lang, 'unit.decibels', { n: formatNumber(value, lang) });
    }
    if (kind === 'hz') return t(lang, 'unit.hertz', { n: formatNumber(value, lang) });
    return formatNumber(value, lang);
  }

  function pad(n) {
    return n < 10 ? '0' + n : String(n);
  }

  function formatClock(seconds) {
    var s = Math.floor(Number(seconds));
    if (!isFinite(s) || s < 0) return '';
    if (s >= 1500000000) {
      var date = new Date(s * 1000);
      return pad(date.getHours()) + ':' + pad(date.getMinutes()) + ':' + pad(date.getSeconds());
    }
    return Math.floor(s / 3600) + ':' + pad(Math.floor(s / 60) % 60) + ':' + pad(s % 60);
  }

  function signalQuality(rssi) {
    if (typeof rssi !== 'number' || !isFinite(rssi) || rssi >= 0) return null;
    if (rssi >= -60) return 'about.q_great';
    if (rssi >= -70) return 'about.q_good';
    if (rssi >= -80) return 'about.q_weak';
    return 'about.q_bad';
  }

  function utf8Length(text) {
    try {
      return encodeURIComponent(String(text)).replace(/%[0-9A-F]{2}/g, 'x').length;
    } catch (e) {
      return Infinity;
    }
  }

  function volumePercent(level) {
    if (typeof level !== 'number' || !isFinite(level)) return null;
    return Math.round(Math.min(1, Math.max(0, level)) * 100);
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
    else if (has(NUMBER_RANGES, kind)) ok = n >= NUMBER_RANGES[kind][0] && n <= NUMBER_RANGES[kind][1];
    else return { rule: { key: 'rule.other' } };
    return ok ? { value: n } : { rule: KIND_RULES[kind] };
  }

  function readValue(kind, raw) {
    var s = String(raw === null || raw === undefined ? '' : raw);
    if (kind === 'secret') return s.trim() ? { value: s.trim() } : { skip: true };
    if (kind === 'text') return s.trim() ? { value: s.trim() } : { rule: { key: 'rule.empty' } };
    if (kind === 'longtext') return s.trim() ? { value: s } : { rule: { key: 'rule.empty' } };
    if (kind === 'choice') return { value: s === '' ? null : s };
    if (kind === 'bool') return { value: raw === true };
    return checkValue(kind, s);
  }

  function loadedOf(spec, loaded) {
    if (!loaded) return undefined;
    if (spec.group) {
      var list = loaded[spec.group];
      return Array.isArray(list) ? list[spec.index] : undefined;
    }
    return loaded[spec.name];
  }

  function buildPatch(specs, loaded, values) {
    var patch = {};
    var errors = [];
    var groups = {};
    specs.forEach(function (spec) {
      if (!has(values, spec.name)) return;
      var got = readValue(spec.kind, values[spec.name]);
      if (got.rule) {
        errors.push({ name: spec.name, rule: got.rule });
        return;
      }
      if (got.skip) return;
      if (spec.kind !== 'secret' && loaded && got.value === loadedOf(spec, loaded)) return;
      if (spec.group) {
        groups[spec.group] = groups[spec.group] || {};
        groups[spec.group][spec.index] = got.value;
      } else patch[spec.name] = got.value;
    });
    Object.keys(groups).forEach(function (group) {
      var list = loaded && Array.isArray(loaded[group]) ? loaded[group].slice() : [];
      Object.keys(groups[group]).forEach(function (index) {
        list[Number(index)] = groups[group][index];
      });
      patch[group] = list;
    });
    return { patch: patch, errors: errors, empty: Object.keys(patch).length === 0 };
  }

  function changedValues(specs, loaded, values) {
    var changed = {};
    specs.forEach(function (spec) {
      if (spec.kind === 'secret' || !has(values, spec.name)) return;
      var got = readValue(spec.kind, values[spec.name]);
      if (!got.rule && loaded && got.value === loadedOf(spec, loaded)) return;
      changed[spec.name] = values[spec.name];
    });
    return changed;
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
    if (code === 'openai_rejected' && failure.openaiCode) {
      return { text: t(lang, 'err.openai_rejected'), detail: failure.openaiCode };
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
  H.SOUND_FIELDS = SOUND_FIELDS;
  H.EQ_HZ = EQ_HZ;
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
  H.formatClock = formatClock;
  H.signalQuality = signalQuality;
  H.utf8Length = utf8Length;
  H.volumePercent = volumePercent;
  H.checkValue = checkValue;
  H.buildPatch = buildPatch;
  H.changedValues = changedValues;
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

  function freshVolume() {
    return { dragging: false, saving: false, next: null, said: null };
  }

  var state = {
    lang: pickLang(langFromSearch((window.location || {}).search) || readStored(LANG_KEY), navigatorLangs()),
    theme: THEMES.indexOf(readStored(THEME_KEY)) >= 0 ? readStored(THEME_KEY) : 'dark',
    screen: 'loading',
    tab: 'conversation',
    auth: null,
    settings: null,
    device: null,
    logAfter: 0,
    logCount: 0,
    volume: freshVolume(),
    loadingSettings: false,
    loadingDevice: false,
    loadingLog: false,
    loadingVolume: false,
    drafts: null,
    checking: false,
    soundTesting: false,
    update: null,
    updating: false,
    restart: null,
    session: 0,
    ids: 0
  };

  doc.documentElement.setAttribute('data-theme', state.theme);
  doc.documentElement.setAttribute('lang', state.lang);

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
    var input = fieldInput(scope, has(FIELD_ALIASES, path) ? FIELD_ALIASES[path] : path);
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
      if (button.hasAttribute('data-own-state')) return;
      if (on) {
        button.setAttribute('data-was-disabled', button.disabled ? '1' : '');
        button.disabled = true;
      } else if (button.hasAttribute('data-was-disabled')) {
        button.disabled = button.getAttribute('data-was-disabled') === '1';
        button.removeAttribute('data-was-disabled');
      }
    });
  }

  function failure(status, code, message, openaiCode) {
    return { failure: true, status: status, code: code || 'http', message: message || '', openaiCode: openaiCode || '' };
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
        throw failure(resp.status, data && data.error, data && data.message, data && typeof data.openai_code === 'string' ? data.openai_code : '');
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

  function lost(f) {
    if (f && (f.code === 'network' || f.code === 'timeout')) $('[data-banner="offline"]').hidden = false;
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

  function show(screen) {
    state.screen = screen;
    $$('[data-screen]').forEach(function (node) {
      node.hidden = node.getAttribute('data-screen') !== screen;
    });
    $$('[data-only-main]').forEach(function (node) {
      node.hidden = screen !== 'main';
    });
  }

  function showHost() {
    say($('[data-host-opened]'), null, key('host.opened', { host: window.location.host }));
    show('host');
  }

  function showSetup() {
    show('setup');
    $('#setup-password').focus();
  }

  function showOffline() {
    show('offline');
  }

  function showLogin(note) {
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
    state.device = null;
    state.logAfter = 0;
    state.logCount = 0;
    state.volume = freshVolume();
    state.loadingSettings = false;
    state.loadingDevice = false;
    state.loadingLog = false;
    state.loadingVolume = false;
    state.checking = false;
    state.update = null;
    state.restart = null;
    updateIdle();
    var fileInput = $('#update-file');
    if (fileInput) fileInput.value = '';
    $('[data-action="check-openai"]').disabled = false;
    $('[data-banner="settings"]').hidden = true;
    $('[data-banner="offline"]').hidden = true;
    clearLog();
    renderDevice();
    $$('form').forEach(function (form) {
      pending(form, false);
      form.reset();
      clearErrors(form);
    });
    $$('[data-status]').forEach(function (node) {
      say(node, null, null);
    });
    lockUnloaded();
    updateVolume();
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
    loadSettings();
  }

  function tabFromHash() {
    var name = String(window.location.hash || '').replace('#', '');
    return TABS.indexOf(name) >= 0 ? name : 'conversation';
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
    if (state.screen !== 'main') return;
    if (name === 'log') pollLog();
    else if (name === 'about') refreshDevice();
    else if (name === 'sound') refreshVolume();
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
      var changed = changedValues(SPECS[name], loadedSection(name), formValues(form, prefixOf(name), SPECS[name]));
      if (Object.keys(changed).length) drafts[name] = changed;
    });
    return Object.keys(drafts).length ? drafts : null;
  }

  function unsavedForms() {
    if (!state.settings) return [];
    return Object.keys(SPECS).filter(function (name) {
      var form = $('[data-form="' + name + '"]');
      if (!form) return false;
      var values = formValues(form, prefixOf(name), SPECS[name]);
      var typed = SPECS[name].some(function (spec) {
        return spec.kind === 'secret' && String(values[spec.name] || '').trim() !== '';
      });
      return typed || Object.keys(changedValues(SPECS[name], loadedSection(name), values)).length > 0;
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
    $$('[data-applies]').forEach(function (node) {
      node.textContent = tr('conversation.applies');
    });
    renderHints();
    updateLanguageNote();
    updateVolume();
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
    if (name === 'wake') return { wake_word: settings.sound.wake_word };
    return settings[name];
  }

  function prefixOf(name) {
    return name === 'wake' ? 'sound.' : name + '.';
  }

  function sectionOf(name) {
    return name === 'wake' ? 'sound' : name;
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
      else fillValue(input, spec.kind, loadedOf(spec, loaded));
      clearField(input);
    });
    if (name === 'assistant') {
      state.instructionsFor = null;
      updateLanguageNote();
    }
    if (name === 'openai') updateLiveFields();
  }

  function updateLiveFields() {
    var select = $('#openai-model');
    var live = !!select && String(select.value).indexOf('gpt-live') === 0;
    $$('[data-realtime-only]').forEach(function (node) {
      node.hidden = live;
    });
    $('#openai-live-note').hidden = !live;
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
    body[sectionOf(name)] = patch;
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
    var built = buildPatch(SPECS[name], loadedSection(name), formValues(form, prefixOf(name), SPECS[name]));
    if (built.errors.length) {
      showRuleErrors(form, prefixOf(name), built.errors);
      return;
    }
    if (built.empty) {
      say(statusOf(form), null, key('nothing'));
      return;
    }
    if (name === 'assistant') {
      saveSection(name, built.patch, form, function () {
        var switched = has(built.patch, 'language');
        say(statusOf(form), 'ok', key(switched ? 'assistant.language_switched' : 'saved'));
      });
    } else if (name === 'sound' || name === 'wake') {
      saveSection(name, built.patch, form, function () {
        say(statusOf(form), 'ok', key('saved_plain'));
      });
    } else saveSection(name, built.patch, form);
  }

  function switchInstructions(language) {
    var area = $('#assistant-instructions');
    if (!area || !state.settings || state.instructionsFor === language) return;
    state.instructionsFor = language;
    if (language === state.settings.assistant.language) {
      area.value = state.settings.assistant.instructions;
      markUnsaved();
      return;
    }
    call('GET', 'api/instructions?language=' + encodeURIComponent(language)).then(function (data) {
      var select = $('#assistant-language');
      if (!select || select.value !== language || typeof data.instructions !== 'string') return;
      area.value = data.instructions;
      clearField(area);
      markUnsaved();
    }).then(null, function (f) {
      state.instructionsFor = null;
      report(f);
    });
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

  function checkOpenai() {
    var node = $('[data-status="openai-check"]');
    var button = $('[data-action="check-openai"]');
    if (state.checking) return;
    if (!state.settings) {
      say(node, 'error', key('settings.not_loaded'));
      loadSettings();
      return;
    }
    if (!state.settings.openai.api_key_set) {
      say(node, 'error', key('err.openai_no_key'));
      return;
    }
    state.checking = true;
    button.disabled = true;
    say(node, 'wait', key('openai.check_wait'));
    call('POST', 'api/openai/test', {}, CHECK_MS).then(function (data) {
      say(node, 'ok', key('openai.check_ok', { model: String(data.model || '?'), voice: String(data.voice || '?') }));
    }, function (f) {
      say(node, 'error', described(f));
      lost(f);
    }).then(function () {
      state.checking = false;
      button.disabled = false;
    });
  }

  function testSound() {
    var node = $('[data-status="sound-test"]');
    var button = $('[data-action="sound-test"]');
    if (state.soundTesting) return;
    state.soundTesting = true;
    button.disabled = true;
    call('POST', 'api/sound/test', {}).then(function () {
      say(node, 'ok', key('volume.test_ok'));
    }, function (f) {
      say(node, 'error', f.code === 'busy' ? key('volume.test_busy') : described(f));
      lost(f);
    }).then(function () {
      state.soundTesting = false;
      button.disabled = false;
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

  function defaultOf(name, spec) {
    if (name === 'timing') return DEFAULTS.timing[spec.name];
    return spec.group ? DEFAULTS.sound.eq : DEFAULTS.sound[spec.name];
  }

  function fillDefaults(name) {
    var form = $('[data-form="' + name + '"]');
    clearErrors(form);
    SPECS[name].forEach(function (spec) {
      var input = fieldInput(form, prefixOf(name) + spec.name);
      if (input) input.value = formatNumber(defaultOf(name, spec), state.lang);
    });
    markUnsaved();
    say(statusOf(form), null, key('form.defaults_done'));
  }

  function submitSetup(form) {
    clearErrors(form);
    var password = fieldInput(form, 'password').value;
    var repeat = fieldInput(form, 'repeat').value;
    if (password.length < MIN_PASSWORD) return showRuleErrors(form, '', [{ name: 'password', rule: { key: 'password.short' } }]);
    if (utf8Length(password) > MAX_PASSWORD_BYTES) return showRuleErrors(form, '', [{ name: 'password', rule: { key: 'password.long' } }]);
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
    if (utf8Length(fresh) > MAX_PASSWORD_BYTES) return showRuleErrors(form, '', [{ name: 'new', rule: { key: 'password.long' } }]);
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

  function currentVolume() {
    return state.settings && state.settings.sound ? volumePercent(state.settings.sound.volume) : null;
  }

  function showVolume(level) {
    var slider = $('#volume');
    var label = $('[data-volume-value]');
    if (level === null) {
      label.textContent = '—';
      slider.removeAttribute('aria-valuetext');
      return;
    }
    var shown = tr('volume.value', { n: formatNumber(level, state.lang) });
    label.textContent = shown;
    slider.setAttribute('aria-valuetext', shown);
  }

  function updateVolume() {
    var slider = $('#volume');
    if (!slider) return;
    var level = currentVolume();
    slider.disabled = level === null;
    if (level === null) state.volume.dragging = false;
    if (state.volume.dragging || state.volume.saving) return;
    if (level !== null) slider.value = String(level);
    showVolume(level);
  }

  function changeVolume() {
    var slider = $('#volume');
    var level = Number(slider.value);
    if (slider.disabled || !isFinite(level)) return;
    if (state.volume.saving) state.volume.next = level;
    else if (level !== currentVolume()) saveVolume(level);
  }

  function saveVolume(level) {
    var volume = state.volume;
    var status = $('[data-status="volume"]');
    volume.saving = true;
    volume.next = null;
    call('PUT', 'api/volume', { volume: level / 100 }).then(function (data) {
      var next = volume.next;
      volume.saving = false;
      volume.next = null;
      if (state.settings && state.settings.sound) {
        state.settings.sound.volume = typeof data.volume === 'number' ? data.volume : level / 100;
      }
      if (status._say && status._say.describe === volume.said) say(status, null, null);
      if (next !== null && next !== level && state.volume === volume) saveVolume(next);
      else updateVolume();
    }, function (f) {
      volume.saving = false;
      volume.next = null;
      volume.said = described(f);
      say(status, 'error', volume.said);
      updateVolume();
    });
  }

  function refreshVolume() {
    if (state.loadingVolume || state.restart || state.updating || state.screen !== 'main' || !state.settings) return;
    state.loadingVolume = true;
    call('GET', 'api/device').then(function (data) {
      if (state.settings && state.settings.sound && typeof data.volume === 'number' && !state.volume.dragging && !state.volume.saving) {
        state.settings.sound.volume = data.volume;
        updateVolume();
      }
    }).then(null, function (f) {
      report(f);
      lost(f);
    }).then(function () {
      state.loadingVolume = false;
    });
  }

  function renderDevice() {
    var info = state.device;
    var values = {
      name: info && info.name ? info.name : '—',
      version: info && info.version ? info.version : '—',
      ip: info && info.ip ? info.ip : '—',
      signal: '—'
    };
    var quality = info ? signalQuality(info.rssi) : null;
    if (quality) values.signal = tr('about.signal_value', { q: tr(quality), n: info.rssi });
    Object.keys(values).forEach(function (name) {
      var node = $('[data-about="' + name + '"]');
      if (node) node.textContent = values[name];
    });
    renderUpdate();
  }

  function renderUpdate() {
    var info = state.update;
    var current = info && info.current ? info.current : state.device && state.device.version ? state.device.version : '—';
    var currentNode = $('[data-update="current"]');
    var latestNode = $('[data-update="latest"]');
    var install = $('[data-action="update-install"]');
    if (!currentNode || !latestNode || !install) return;
    currentNode.textContent = current;
    latestNode.textContent = info && info.latest ? info.latest : '—';
    var can = !!(info && info.available && info.latest);
    install.hidden = !can;
    if (can) install.textContent = tr('update.install', { version: info.latest });
  }

  function setUpdateBusy(on) {
    ['update-check', 'update-install', 'update-upload'].forEach(function (name) {
      var button = $('[data-action="' + name + '"]');
      if (button) button.disabled = on;
    });
    var input = $('#update-file');
    if (input) input.disabled = on;
  }

  function updateIdle() {
    state.updating = false;
    setUpdateBusy(false);
  }

  function checkUpdate() {
    if (state.updating) return;
    var node = $('[data-status="update"]');
    state.updating = true;
    setUpdateBusy(true);
    say(node, 'wait', key('update.check_wait'));
    call('GET', 'api/update', null, CHECK_MS).then(function (data) {
      state.update = data;
      renderUpdate();
      if (data.available && data.latest) say(node, 'ok', key('update.available', { version: String(data.latest) }));
      else say(node, 'ok', key(data.latest ? 'update.up_to_date' : 'update.none'));
    }, function (f) {
      say(node, 'error', described(f, 'update'));
      lost(f);
    }).then(updateIdle);
  }

  function watchRestart(node) {
    var watch = { down: false, tries: 0 };
    state.restart = watch;
    say(node, 'wait', key('update.restarting'));
    function finish() {
      say(node, 'ok', key('update.back'));
      window.setTimeout(function () {
        if (state.restart === watch) window.location.reload();
      }, 1500);
    }
    function later() {
      if (state.restart !== watch) return;
      if (watch.tries >= RESTART_TRIES) {
        state.restart = null;
        say(node, 'error', key('update.restart_timeout'));
        updateIdle();
        return;
      }
      window.setTimeout(step, RESTART_POLL_MS);
    }
    function step() {
      if (state.restart !== watch) return;
      watch.tries += 1;
      request('GET', 'api/state', null, RESTART_REQUEST_MS).then(function () {
        if (state.restart !== watch) return;
        if (watch.down || watch.tries > RESTART_SURE_TRIES) finish();
        else later();
      }, function () {
        if (state.restart !== watch) return;
        watch.down = true;
        later();
      });
    }
    window.setTimeout(step, RESTART_FIRST_MS);
  }

  function installUpdate() {
    var info = state.update;
    if (state.updating || !info || !info.available || !info.latest) return;
    var version = String(info.latest);
    var node = $('[data-status="update"]');
    var session = state.session;
    confirmAction(function (lang) {
      return t(lang, 'update.confirm', { version: version });
    }, 'update.confirm_ok').then(function (yes) {
      if (!yes || state.updating || session !== state.session) return;
      state.updating = true;
      setUpdateBusy(true);
      say(node, 'wait', key('update.installing'));
      call('POST', 'api/update', {}, INSTALL_MS).then(function () {
        watchRestart(node);
      }, function (f) {
        say(node, 'error', described(f, 'update'));
        lost(f);
        updateIdle();
      });
    });
  }

  function sendFile(file, onProgress) {
    return new Promise(function (resolve, reject) {
      var xhr = new window.XMLHttpRequest();
      xhr.open('POST', 'api/update/upload');
      xhr.timeout = UPLOAD_MS;
      xhr.setRequestHeader('Content-Type', 'application/octet-stream');
      xhr.setRequestHeader('Accept', 'application/json');
      if (xhr.upload) {
        xhr.upload.onprogress = function (e) {
          if (e && e.lengthComputable && e.total) onProgress(Math.floor((e.loaded * 100) / e.total));
        };
      }
      xhr.onload = function () {
        var data = null;
        try {
          data = JSON.parse(xhr.responseText);
        } catch (e) {
          data = null;
        }
        if (xhr.status >= 200 && xhr.status < 300) resolve(data || {});
        else reject(failure(xhr.status, data && data.error, data && data.message));
      };
      xhr.onerror = function () {
        reject(failure(0, 'network', ''));
      };
      xhr.ontimeout = function () {
        reject(failure(0, 'timeout', ''));
      };
      xhr.send(file);
    });
  }

  function uploadUpdate() {
    if (state.updating) return;
    var node = $('[data-status="update-file"]');
    var input = $('#update-file');
    var file = input.files && input.files[0];
    if (!file) {
      say(node, 'error', key('update.no_file'));
      return;
    }
    if (file.size > UPLOAD_MAX) {
      say(node, 'error', key('update.file_too_big'));
      return;
    }
    var name = String(file.name || '');
    var session = state.session;
    confirmAction(function (lang) {
      return t(lang, 'update.file_confirm', { name: name });
    }, 'update.confirm_ok').then(function (yes) {
      if (!yes || state.updating || session !== state.session) return;
      state.updating = true;
      setUpdateBusy(true);
      say(node, 'wait', key('update.uploading', { n: 0 }));
      sendFile(file, function (n) {
        if (session !== state.session) return;
        say(node, 'wait', key(n >= 100 ? 'update.upload_done' : 'update.uploading', { n: n }));
      }).then(function () {
        if (session === state.session) watchRestart(node);
      }, function (f) {
        if (session !== state.session) return;
        if (f.code === 'login_required' && state.screen === 'main') {
          expire();
          return;
        }
        say(node, 'error', described(f, 'update'));
        lost(f);
        updateIdle();
      });
    });
  }

  function refreshDevice() {
    if (state.loadingDevice || state.updating || state.screen !== 'main') return;
    state.loadingDevice = true;
    call('GET', 'api/device').then(function (data) {
      state.device = data;
      renderDevice();
    }).then(null, function (f) {
      report(f);
      lost(f);
    }).then(function () {
      state.loadingDevice = false;
    });
  }

  function clearLog() {
    var list = $('[data-log-list]');
    while (list.firstChild) list.removeChild(list.firstChild);
    state.logCount = 0;
    state.logAfter = 0;
    $('[data-log-empty]').hidden = false;
    $('[data-log-box]').hidden = true;
  }

  function addLogLines(lines) {
    var list = $('[data-log-list]');
    var box = $('[data-log-box]');
    var stick = !box.clientHeight || box.scrollHeight - box.scrollTop - box.clientHeight < 8;
    lines.forEach(function (line) {
      if (!line || typeof line.seq !== 'number' || line.seq <= state.logAfter) return;
      state.logAfter = line.seq;
      list.appendChild(el('li', {}, [
        el('span', { 'class': 'log-time', text: formatClock(line.t) }),
        el('span', { 'class': 'log-text', text: String(line.text) })
      ]));
      state.logCount += 1;
    });
    while (state.logCount > LOG_MAX) {
      list.removeChild(list.firstChild);
      state.logCount -= 1;
    }
    $('[data-log-empty]').hidden = state.logCount > 0;
    box.hidden = state.logCount === 0;
    if (stick && state.logCount > 0) box.scrollTop = box.scrollHeight;
  }

  function pollLog() {
    if (state.loadingLog || state.restart || state.updating || state.screen !== 'main' || state.tab !== 'log' || doc.visibilityState === 'hidden') return;
    state.loadingLog = true;
    call('GET', 'api/log?after=' + state.logAfter).then(function (data) {
      addLogLines(Array.isArray(data.lines) ? data.lines : []);
    }).then(null, function (f) {
      report(f);
      lost(f);
    }).then(function () {
      state.loadingLog = false;
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

  function numberField(path, id, kind, unitKey, labelNode) {
    var hintId = id + '-hint';
    var decimal = kind !== 'count';
    var input = el('input', {
      id: id,
      type: 'text',
      inputmode: decimal ? 'decimal' : 'numeric',
      'data-field': path,
      'data-decimal': decimal,
      autocomplete: 'off',
      'aria-describedby': hintId
    });
    var parts = [input];
    if (unitKey) parts.push(el('span', { 'class': 'unit', i18n: unitKey }));
    return {
      input: input,
      hintId: hintId,
      box: el('div', { 'class': 'field' }, [labelNode, el('div', { 'class': 'with-unit' }, parts)])
    };
  }

  function buildTimingFields() {
    TIMING_FIELDS.forEach(function (field) {
      var id = 'timing-' + field.name;
      var built = numberField('timing.' + field.name, id, field.kind, field.kind === 'count' ? null : 'unit.s',
        el('label', { 'for': id, i18n: 'timing.' + field.name + '.label' }));
      built.box.appendChild(el('p', { 'class': 'hint', id: built.hintId, 'data-timing-hint': field.name }));
      $(field.group === 'main' ? '[data-timing-main]' : '[data-timing-more]').appendChild(built.box);
    });
  }

  function buildSoundFields() {
    SOUND_FIELDS.forEach(function (field) {
      var id = 'sound-' + field.name;
      var built = numberField('sound.' + field.name, id, field.kind, field.unit,
        el('label', { 'for': id, i18n: 'sound.' + field.name + '.label' }));
      built.box.appendChild(el('p', { 'class': 'hint', id: built.hintId, 'data-sound-hint': field.name }));
      $('[data-sound-main]').appendChild(built.box);
    });
    EQ_HZ.forEach(function (hz, i) {
      var id = 'sound-' + eqName(i);
      var built = numberField('sound.' + eqName(i), id, 'eq', 'unit.db',
        el('label', { 'for': id, 'data-eq-label': hz }));
      built.input.removeAttribute('aria-describedby');
      $('[data-sound-eq]').appendChild(built.box);
    });
  }

  function renderHints() {
    TIMING_FIELDS.forEach(function (field) {
      var node = $('[data-timing-hint="' + field.name + '"]');
      if (!node) return;
      node.textContent = tr('timing.' + field.name + '.hint') + ' ' +
        tr('form.default', { v: formatSetting(field.kind, DEFAULTS.timing[field.name], state.lang) });
    });
    SOUND_FIELDS.forEach(function (field) {
      var node = $('[data-sound-hint="' + field.name + '"]');
      if (!node) return;
      node.textContent = tr('sound.' + field.name + '.hint') + ' ' +
        tr('form.default', { v: formatSetting(field.kind, DEFAULTS.sound[field.name], state.lang) });
    });
    $$('[data-eq-label]').forEach(function (node) {
      node.textContent = tr('unit.hertz', { n: node.getAttribute('data-eq-label') });
    });
    var vars = {
      'openai.vad_hint': { v: formatNumber(DEFAULTS.openai.vad_threshold, state.lang) },
      'openai.silence_hint': { v: tr('unit.millis', { n: DEFAULTS.openai.silence_ms }) }
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
    renderDevice();
    showVolume(currentVolume());
    $$('[data-say]').forEach(renderSaid);
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
    else if (action === 'check-openai') checkOpenai();
    else if (action === 'sound-test') testSound();
    else if (action === 'update-check') checkUpdate();
    else if (action === 'update-install') installUpdate();
    else if (action === 'update-upload') uploadUpdate();
    else if (action === 'reset-instructions') resetInstructions();
    else if (action === 'timing-defaults') fillDefaults('timing');
    else if (action === 'sound-defaults') fillDefaults('sound');
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
    if (input.id === 'openai-model') updateLiveFields();
    if (input.id === 'assistant-language') {
      updateLanguageNote();
      switchInstructions(input.value);
    }
    if (input.closest && input.closest('[data-form]')) markUnsaved();
  }

  function onLeave(event) {
    if (state.screen !== 'main' || !unsavedForms().length) return;
    event.preventDefault();
    event.returnValue = true;
  }

  function poll() {
    if (state.screen !== 'main' || doc.visibilityState === 'hidden' || state.restart || state.updating) return;
    if (!state.settings) loadSettings();
    var offline = !$('[data-banner="offline"]').hidden;
    if (state.tab === 'about' || offline) refreshDevice();
    if (state.tab === 'sound') refreshVolume();
  }

  function startVolume() {
    var slider = $('#volume');
    slider.addEventListener('input', function () {
      if (slider.disabled) return;
      state.volume.dragging = true;
      showVolume(Number(slider.value));
    });
    slider.addEventListener('change', function () {
      state.volume.dragging = false;
      changeVolume();
    });
    ['pointerup', 'mouseup', 'touchend', 'touchcancel', 'blur'].forEach(function (type) {
      slider.addEventListener(type, function () {
        state.volume.dragging = false;
      });
    });
  }

  function start() {
    buildTimingFields();
    buildSoundFields();
    applyText(doc);
    markLang();
    markUrl(state.lang);
    markTheme();
    renderHints();
    lockUnloaded();
    clearLog();
    startVolume();
    updateVolume();
    doc.addEventListener('submit', onSubmit);
    doc.addEventListener('click', onClick);
    doc.addEventListener('input', onInput);
    doc.addEventListener('change', onInput);
    doc.addEventListener('visibilitychange', function () {
      poll();
      pollLog();
    });
    window.addEventListener('beforeunload', onLeave);
    window.addEventListener('hashchange', function () {
      if (state.screen !== 'main') return;
      selectTab(tabFromHash());
      window.scrollTo(0, 0);
    });
    window.setInterval(poll, POLL_MS);
    window.setInterval(pollLog, LOG_MS);
    boot();
  }

  if (doc.readyState === 'loading') doc.addEventListener('DOMContentLoaded', start);
  else start();
})(window);
