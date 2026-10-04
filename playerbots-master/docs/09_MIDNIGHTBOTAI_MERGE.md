# 09 — Слияние с MidnightBotAI (LrdPsychoChains WOW-HUB)

## Что это

В модуль встроены исходники **MidnightBotAI** — пакет ботов для TrinityCore 12.1.0
из репозитория <https://github.com/psychochaingang/LrdPsychoChains-WOW-HUB>
(проект `projects/midnightbotai`, версия 4.8).

Лицензия их кода — **GPLv3** (совместимо с TrinityCore); атрибуция сохраняется в
заголовках файлов `MidnightBotAI.cpp`, `MidnightBotMgr.cpp`, `MidnightBotMgr.h`.

## Принцип слияния (v2 — их команды ЗАМЕНЕНЫ нашими)

Их `.mbot`/`.mb` **не регистрируются** (`new MidnightBotAICommandScript()` выключен в
`AddSC_midnight_bot_ai()`). Их уникальный функционал доступен под нашим именем:

| Наша команда | Эквивалент `.mbot` | Что делает |
|---|---|---|
| `.playerbots stats` | `stats` | метры урона/хила (работает и из консоли) |
| `.playerbots role <имя> <tank\|healer\|dps\|none>` | `role` | роль их движка |
| `.playerbots assist <имя> [on\|off]` | `assist` | ассист |
| `.playerbots attack <имя>` | `attack` | атака выделенной цели |
| `.playerbots boost <имя> [ур]` / `boostall` | `boost` | уровень+гир+автоталанты |
| `.playerbots party <имя>\|all` | `party` | вступление в группу владельца |
| `.playerbots loot [show\|method …\|threshold …]` | `loot` | правила добычи группы |
| `.playerbots resurrect <имя>` | `resurrect` | подъём |
| `.playerbots learn [имя]` | `learn` | досказание спеллов роли |
| (наши) `create/add/remove/list/summon/followme/stay/equip/book/hero/talents/dummy` | `create/add/remove/list/summon/follow/stay/…` | уже были нашими |

Не переносились (диагностика/редкое): `where/vis/debug/lead/come/enable/disable` —
их аналоги: `.playerbots list/ping`, Server.log.

**Адопция:** первый вызов gameplay-команды на нашем боте вызывает
`MidnightBotMgr::AdoptBot` — бот входит в их активный ростер (с владельцем = тот, кто
выполнил команду). Правила:

- **QA-боты не адоптируются** (движок не подключается — QA остаётся на нашем dummy/sweep);
- бот в режиме манекена — сначала `dummy stop`;
- адоптированный бот **исключается из нашего тика AI** (`PlayerbotMgr::UpdateAI` проверяет
  `IsBotActive`) — поведение ведёт их Update; наши команды работают через общий entry.ai;
- `.playerbots remove` вызывает `ForgetBot` — бот уходит и из их ростера.

Регистрация: `AddSC_playerbots()` вызывает `AddSC_midnight_bot_ai()` — их
`WorldScript`/`UnitScript` (движок+метрики) остаются, `CommandScript` — выключен.

## Файлы

- `src/bot/MidnightBotAI.cpp`   — команды `.mbot`/`.mb`
- `src/bot/MidnightBotMgr.cpp`  — движок: спавн/сессии, бой (касты-виндаупы,
  spellPace), роли/агро, метры, лоут, автоталенты, гир-подбор, парти, ростер
- `src/bot/MidnightBotMgr.h`
- `conf/MidnightBotAI.conf.dist` — их ключи

`apply_windows.cmd` копирует `src/bot/*.cpp|*.h` как обычно (robocopy), отдельных
шагов не нужно. Их `midnight_bot_ai_loader.cpp` НЕ копируется — регистрация идёт
через наш `AddSC_playerbots()`.

## После обновления — обязательный re-configure!

Новые `.cpp` подхватываются `file(GLOB)` только на **configure**:

```
cmake -S . -B build -A x64
cmake --build build --config Release -j 8
```

(иначе — LNK2019 на `AddSC_midnight_bot_ai` / их символы).

## Конфиг

Скопировать `conf/MidnightBotAI.conf.dist` в каталог `worldserver.conf.d/`
**с именем `MidnightBotAI.conf`** (worldserver подхватывает только `*.conf`, файлы
`.dist` игнорирует — без этого в логе будут warning'и `Missing name MidnightBotAI.*`)
либо слить ключи в worldserver.conf.

Дефолты безопасны для сосуществования:

- `MidnightBotAI.Spawning = 0`, `AutoSpawn = 0`, `Bootstrap = ""` — **ничего не
  спавнится и не создаётся само**; их боты появляются только по `.mbot create/spawn`;
- `LevelSync/AutoTalents` действуют только на их (привязанных к владельцу) ботов,
  QA-боты `.playerbots` не затрагиваются.

## Аккаунты

Их боты живут на аккаунтах `MBOT_<Имя>` (префикс `MidnightBotAI.AccountPrefix`).
Идентификаторы новых аккаунтов берутся автоинкрементом БД: если в БД уже есть
ваши аккаунты 9000–9599 — новые уйдут выше 9599. Если диапазон почти пуст,
сначала создайте наших ботов (фикс. id 9000+), потом `.mbot create`.

## Что НЕ переносилось

Их конфликтующие с нами вещи намеренно не тронуты: QA-слой (DummyLog/sweep/
COVERAGE/mechanics) — только наш; их `.mbot` не управляет QA-ботами и наоборот.
