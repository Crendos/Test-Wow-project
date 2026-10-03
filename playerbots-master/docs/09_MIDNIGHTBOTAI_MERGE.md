# 09 — Слияние с MidnightBotAI (LrdPsychoChains WOW-HUB)

## Что это

В модуль встроены исходники **MidnightBotAI** — пакет ботов для TrinityCore 12.1.0
из репозитория <https://github.com/psychochaingang/LrdPsychoChains-WOW-HUB>
(проект `projects/midnightbotai`, версия 4.8).

Лицензия их кода — **GPLv3** (совместимо с TrinityCore); атрибуция сохраняется в
заголовках файлов `MidnightBotAI.cpp`, `MidnightBotMgr.cpp`, `MidnightBotMgr.h`.

## Принцип слияния

Обе системы работают **одновременно** в одном сервере:

| Система | Команды | Роль |
|---|---|---|
| Наша (playerbots) | `.playerbots create/add/equip/dummy/talents/hero/book/...` | QA-боты: DummyLog, sweep, COVERAGE, механики, герой-дерева |
| Их (MidnightBotAI) | `.mbot …` = `.mb …` (алиасы) | Игровые компаньоны: роли, агро, метры, лоут, буст, автоталенты |

Регистрация: `AddSC_playerbots()` вызывает `AddSC_midnight_bot_ai()` — их
`WorldScript`/`UnitScript`/`CommandScript` независимы от наших (разные классы,
разные хуки, коллизий нет; их хелперы — в анонимных неймспейсах).

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
(worldserver подхватывает `*.conf` автоматически) либо слить ключи в worldserver.conf.

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
