# 03 — Внедрение на Windows через «x64 Native Tools Command Prompt for VS 2022»

Шаги рассчитаны на уже рабочую сборку TrinityCore master у пользователя
(Boost/OpenSSL/MySQL пути у вас уже прописаны в CMakeCache — скрипту/вам
узнавать их заново не нужно). Сборка VS 2022, x64, Dynamic Scripts
(scripts_custom.dll — ваш режим уже такой).

## Быстрый путь (два этапа: сначала патч, потом сборка)

1. Пуск → **Visual Studio 2022 → x64 Native Tools Command Prompt for VS 2022**.
2. **Патч — ничего не компилирует:**
   ```bat
   :: пример — подставьте свои пути
   C:\...\Test-Wow-project\playerbots-master\tools\apply_windows.cmd ^
       C:\TrinityCore\Source
   ```
   Скрипт проверяет, что он запущен именно из native-prompt, патчит ядро
   (`git apply --3way`, при частичном конфликте — по docs\02) и копирует
   модуль в `src\server\scripts\Custom\playerbots`. **Компиляции нет** —
   это этап, на который вносятся свои правки в исходники (например, фиксы
   паладина). Внимание: повторный запуск перезаписывает файлы, скопированные
   из `src\bot` — правки в одноимённых файлах затрутся; держите свои правки
   в новых файлах или коммитьте их в `playerbots-master`.
3. **Сборка — отдельной командой, когда все правки внесены:**
   ```bat
   C:\...\Test-Wow-project\playerbots-master\tools\build_windows.cmd ^
       C:\TrinityCore\Build RelWithDebInfo
   ```
   Переконфигурирует CMake (все ваши пути остаются в кеше) и собирает цели
   **game → scripts (или scripts_custom) → worldserver**.

## Ручной путь (те же шаги одной строкой каждая — в том же prompt)

```bat
:: 0) предполагаем:
set SRC=C:\TrinityCore\Source
set BUILD=C:\TrinityCore\Build
set MOD=C:\...\Test-Wow-project\playerbots-master

:: 1) применить патч ядра (WorldSession.*, CharacterHandler.cpp, loader)
cd /d %SRC%
git apply --3way --verbose "%MOD%\patches\0001-core-integration.diff"
::    если 3way не помог: git apply --reject ... и разобрать .rej по docs/02

:: 2) положить файлы модуля
mkdir "%SRC%\src\server\scripts\Custom\playerbots"
copy %MOD%\src\bot\*.* "%SRC%\src\server\scripts\Custom\playerbots\"

:: 3) переконфигурировать без изменения настроек (все ваши пути в кеше)
cmake %BUILD%

:: 4) сборка затронутых проектов в нужном порядке
cmake --build %BUILD% --config RelWithDebInfo --target game -- /m
::    SCRIPTS=static  -> цель "scripts"         (агрегатная lib, модуль попадёт в worldserver.exe)
::    SCRIPTS=dynamic -> цель "scripts_custom"  (отдельная dll)
cmake --build %BUILD% --config RelWithDebInfo --target scripts -- /m
cmake --build %BUILD% --config RelWithDebInfo --target worldserver -- /m
```

Зачем именно такой порядок: патч меняет **game** (WorldSession/CharacterHandler);
модуль компилируется внутри **scripts** (static) либо **scripts_custom** (dynamic —
оба случая build_windows.cmd определяет сам по CMakeCache.txt);
**worldserver** линкуется последним. Первая сборка `scripts` при static — долгая
(компилируются все модули скриптов), это нормально, дальше — инкрементально.

## Первичная инициализация данных (делается один раз)

1. В world-БД выполнить `playerbots-master\sql\world_playerbots_rotation.sql`
   (плюс при необходимости `world_playerbots_class_knowledge.sql`,
   `world_playerbots_boss_rules.sql`, `world_playerbots_hero_talents.sql` — v6,
   для `.playerbots create hero=...`, см. docs/08).
   Например из того же prompt (если mysql.exe доступен):
   `mysql -u trinity -p world < %MOD%\sql\world_playerbots_rotation.sql`
2. В worldserver.conf дописать ключи из conf/playerbots.conf.dist — минимум:
   `Playerbots.Enabled = 1` (для логов боя также `Playerbots.DummyLogDir`,
   см. docs/08_CREATE_DUMMY_LOG.md).
3. Завести 1..N бот-аккаунтов в auth.account (обычные, как игроков) и персонажей
   на них (classes командами .create/.level из консоли). Имена — латиница, до 12 знаков.
4. Заполнить `world.playerbots_rotation` строками с guid/name/classId этих персонажей
   и опционально `world.playerbots_combat_spells` (примеры — внизу SQL-файла).

## Проверка в игре

- Войти реальным игроком, набрать в чате:
  - `.playerbots add <имя>` — бот зайдёт (увидите его в /who),
  - `.playerbots list`,
  - `.playerbots roster start` — поднять состав из таблицы.
- Бот-тик работает через worldserver-хук OnUpdate; `Playerbots.Log` — категория лога в worldserver.conf.

## Куда смотреть при MSVS-специфике

- Патч применяли к дереву master @ a96d897 (2026-09). Если у вас master новее —
  `git apply --3way` обычно разруливает сам; самое вероятное место конфликта —
  база класса в WorldSession.h (LoginQueryHolder) — разбор по docs/02.
- MSVS может выдать C4265/warnings в наших файлах — на сборку не влияет.
- `scripts_custom` появляется в solution ТОЛЬКО после шага `cmake %BUILD%` (reconfigure).
- **Новые `.cpp` модуля видны только после reconfigure**: TC собирает скрипты через
  `file(GLOB *.cpp)` **на момент конфигурации CMake** — файл, скопированный apply'ем
  после последней конфигурации, сборка не увидит и выдаст ровно такие `LNK2019`
  (неопределённые символы его функций). Лечится одной командой (кэш и генератор
  сохраняются): `cmake -B <TC_BUILD_DIR> -S <TC_SRC>` — и дальше обычная сборка.
  Тот же принцип: apply теперь сам проверяет DummyLog.cpp и подсказывает про
  reconfigure в конце.
- Если сборка worldserver жалуется на libcrypto/libssl — ничего не меняйте: ваши dll'ы
  уже лежат рядом с worldserver.exe, как и раньше.
