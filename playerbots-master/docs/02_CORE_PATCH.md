# 02 — Патч ядра (что, где и зачем), и как это проверено компиляцией

Патч: `patches/0001-core-integration.diff` — применяется к TrinityCore **master**
(сверено на дереве `a96d897`; API этой сверки: `docs/01_ARCHITECTURE.md`).

Файл модуля лежит в `src/bot/` и по инструкции кладётся в исходник ядра
`src/server/scripts/Custom/playerbots/` + одна строка декларации/регистрации
`AddSC_playerbots()` в `src/server/scripts/Custom/custom_script_loader.cpp`
(строка уже входит в diff).

## Что делает патч (3 файла ядра + 1 строка loader)

### `src/server/game/Server/WorldSession.h`
- В паблик секцию добавлен API «бот-сессии»:
  - `MarkAsPlayerBot()` / `IsPlayerBot()` — пометка сессии до старта `World::AddSession_`;
  - `LoginPlayerBot(ObjectGuid)` — вход персонажа в мир без сокетного танца
    `ConnectTo`/`ResumeComms` (реплика `HandleContinuePlayerLogin` без дополнительной SMSG-части).
- Добавлен `bool IsBotLoginPending()` — дословно уважающий правило «CMSG_PLAYER_LOGIN
  валиден только из другой сессии» (для будущих комманд-бриджей).
- Объявлен класс `LoginQueryHolder : public CharacterDatabaseQueryHolder` —
  **вынесен из CharacterHandler.cpp в хедер**, иначе его невозможно сконструировать
  из другого TU (раньше он был приватен хэндлеру). Это самое хрусткое место патча:
  если в master поменяется этот класс — патч скажет git reject.
- Добавлен `#include "QueryHolder.h"` — хедер используется местным (шаблон `SQLQueryHolder<T>`).

### `src/server/game/Server/WorldSession.cpp`
- Тело `LoginQueryHolder::Initialize()` переехало сюда (раньше — в CharacterHandler.cpp).
- `SendPacket()` сверху добавлено `if (m_isPlayerBot) return;` — у бота нет сокета,
  все SMSG quiet-drop.
- `Update()` сверху добавлен быстрый путь: для `m_isPlayerBot`
  `ProcessQueryCallbacks` + `updater.ProcessUnsafe()` + logout по `_logoutTime`
  (без сокетного редакта idle-kick'а, никогда не сработающего).
- В конец файла добавлен `WorldSession::LoginPlayerBot(ObjectGuid guid)`:
  проверка `IsLegitCharacterForAccount`, конструктор `LoginQueryHolder`,
  `CharacterDatabase.DelayQueryHolder(holder)` → `AfterComplete` callbacк в
  `HandlePlayerLogin(holder)` — дословно повторяет `HandleContinuePlayerLogin`.

### `src/server/game/Handlers/CharacterHandler.cpp`
- Удалены `class LoginQueryHolder` и тело `LoginQueryHolder::Initialize()` (переехали выше).

### `src/server/scripts/Custom/custom_script_loader.cpp`
- Добавлена регистрация `AddSC_playerbots();` в `AddCustomScripts()`.

## Модуль (файлы `src/bot/`)
- `PlayerbotMgr.h/.cpp` — менеджер фейковых `WorldSession` и состава (ростер):
  - `AddBot/RemoveBot/RemoveAll` поверх `sCharacterCache` (guid+accountId персонажа —
    **никаких SQL в world-thread**);
  - конструктор `WorldSession` по текущей master-сигнатуре (15 аргументов, см. код);
  - `LoadPlayerBotCombatSpells(...)` — читает `world.playerbots_combat_spells`;
  - ростер из `world.playerbots_rotation` (SQL-схема в `sql/`).
- `PlayerbotAI.h/.cpp` — состояние одного бота: PLANE (блуждание/эмоуты/слова в фоне)
  + COMBAT. Использует только публичное API master: `CastSpell(CastSpellTargetArg, …)`,
  `SpellHistory::IsReady(SpellInfo const*)`, предельный доступ к `MotionMaster`.
- `cs_playerbots.cpp` — команды `.playerbots …` (через современный
  `std::span<ChatCommandBuilder const>` `CommandScript::GetCommands`, `Tail`-тегом
  остальной строки, `rbac::RBAC_PERM_COMMAND_RESET_TALENTS` как временный permission)
  + `playerbots_worldscript : WorldScript` — тикер `sPlayerbotMgr->UpdateAI(diff)`
  через worldserver-хук `OnUpdate(diff)` и автостарт состава из `OnStartup()`
  **без правок в game/lib**.

## Проверка компиляцией (сделано в песочнице)
Окружение: Debian 12 (bookworm), g++ 12.2, cmake 3.31, ninja,
Boost 1.86.0 (filesystem/program_options/regex/locale, shared, конфиг-мода CMake),
mariadb-connector-c 3.3.10 (найден как MariaDB 10.8.8), OpenSSL 3.0.15 headers + system runtime.

Скомпилированы одиночные объекты (полный worldserver не линковался — RAM-лимит песочницы):
- `src/server/game/CMakeFiles/game.dir/Server/WorldSession.cpp.o` ✅
- `src/server/game/CMakeFiles/game.dir/Handlers/CharacterHandler.cpp.o` ✅
- `src/server/scripts/CMakeFiles/scripts.dir/Custom/playerbots/{PlayerbotAI,PlayerbotMgr,cs_playerbots}.cpp.o` ✅
- `src/server/scripts/CMakeFiles/scripts.dir/Custom/custom_script_loader.cpp.o` ✅
`ninja` прогоняется последовательно — 0 ошибок компиляции; синтаксис/API совместимы.

Для пользователя (Windows/VS 16 — см. repo root) порядок тот же: применить diff,
положить `src/bot/*` в `scripts/Custom/playerbots/`, вернуть строку в
`custom_script_loader.cpp`, обновить boost ≥1.74 (TBB/etc helper scripts от TC либо
Boost 1.86+ по умолчанию), собрать solution.

## Известные ослабления MVP (что снимется фазой 1+)
- `IsBotLoginPending()` сейчас нигде не вызывается — зарезервированный gate-крюк.
- Ротация ростера — вручную командой; циклический таймер — следующим шагом.
- Каст-кит: только анти-spam в 2s и без range/сфера/стан-target фильтров
  (Котём/IQ деталь в войд-CastSpell дата из БД — фаза 1).
- Рота-очередь спелов пока общая с БД, без per-class предсустановок.
- Переход session-scope на цикл worldthread (освобождение занятости очереди
  сокетных вызовов в `World::UpdateSessions` врезка со стороны ядра в будущем
  коммите `World.cpp`; текущий патч не затрагивает — меньше конфликтов).
