# Playerbots для TrinityCore master (12.1.0.69497)

Проект собственной бот-системы («playerbots»: боты = настоящие объекты Player,
видимые в /who, группах, гильдиях и БГ) для **TrinityCore master** под клиент
**12.1.0.69497**. Готового порта playerbots под master не существует (все живые
проекты — под WotLK 3.3.5), поэтому система пишется с нуля поверх реального API
текущего master.

Архитектура и каждый фрагмент кода сверены с исходниками master
(коммит `a96d897...`, 2026-09-23).

## Статус

- **MVP-каркас** ✅ — логин/логаут бота из characters, follower на AutoFollow-векторе, минимальный AI. `docs/01–03`.
- **v2 — движение/follow** ✅ — плавное движение через `MovePoint` (ядро само шлёт MonsterMove), followmaster (`.playerbots follow/stay`), защита мастера, дальность спелов. `docs/04`.
- **v3 — классовое знание** ✅ — авто-билд знания из спелбукка бота, per-class таблица `playerbots_class_knowledge`, команда `.playerbots book`. `docs/05`.
- **v4 — ротация + авто-экипировка** ✅ — настоящий GCD (`StartRecoveryTime`, мин 1500мс), каст-тайм останавливает движение, DoT/дебаффы поддерживаются по ауре цели (caster GUID), burst-кулдауны (Recovery≥60с) жмутся в первые 8 сек боя или при цели <30% HP, неудачные касты — в перкомбатный blacklist. Авто-шмот: `score = iLvl*10 + quality`, фильтр брони по классу (plate/mail/leather/cloth), `.playerbots equip <имя>`. `docs/06`.
- **v5 — босс-механики (Midnight)** ✅ — движок правил `world.playerbots_boss_rules` (триггеры: каст/аура босса, аура на боте, HP; действия: interrupt, отбежать/разойтись/шаг в сторону, switch на add, defensive, dispel себя, форс-бурст). Контент: Murder Row → Kystia Manaheart (правила из ядерного босс-скрипта, spell-ID 1:1). `docs/07`.
- **v6 — create + бой с манекеном + лог боя + QA-слой** ✅ — `.playerbots create <account> class=… race=… level=… spec=… hero=… item=… name=…` (серверное создание персонажа по критериям, ник рандом 3–4, TempSession-паттерн CharacterHandler, синхронное сохранение); `.playerbots dummy start|stop <бот> [entry] [x y z] [sweep]` (точка/entry/выделенный манекен, режим высшего приоритета в Update, **QA-sweep** — прогон всех боевых спеллов по разу); `DummyLog` — файловый лог всех аур/бафов (поллинг `GetAppliedAuras`), кастов, **отклонённых кастов `CAST_FAIL`** (возврат `SpellCastResult` из `CastSpell`), урона (`ModifySpellDamageTaken`/`ModifyMeleeDamage`/`ModifyPeriodicDamageAurasTick`/`OnDamage`), хила, проков и `SUMMARY` c `cast_fails` + **QA-отчёт `.qa.txt`** (FINDINGS: unknown_cast_result / resource_starved / cast_without_damage / zero_total_damage / sweep-итог). **Роли разделены**: QA-бот = аккаунт в `Playerbots.QAAccountsStart/End` (по умолч. 9500…9599) — ему sweep/`CAST_FAIL`/быстрые ретраи; игровой бот (9000…9499) — чистая ротация v4/v5. `docs/08`.


Roadmap v5+: точный GCD с хаст-коррекцией, стат-скоринг шмота по классам,
босс-механики (engine правил + proof-of-concept на одном боссе), роуты/патрули,
whisper-управление, компаньоны.

(v6 готов: create персонажей по критериям, режим манекена с полным логом боя —
`docs/08`; остальное из списка остаётся в планах.)


## Порядок внедрения (кратко, детали — в docs/)

**Windows/VS2022 — два этапа**, оба из «x64 Native Tools Command Prompt for VS 2022»
(пошаговая инструкция `docs/03_WINDOWS_VS2022.md`):

1. `tools\apply_windows.cmd <TC_SOURCE_DIR>` — **только патч**: ядро (`git apply`)
   + копия модуля в `Custom\playerbots`, без единой компиляции. Здесь же можно
   внести свои правки в исходники (например, фиксы паладина).
2. `tools\build_windows.cmd <TC_BUILD_DIR> [CONFIG]` — **сборка всего вместе**:
   cmake reconfigure → game → scripts/scripts_custom → worldserver.

1. Клонировать master, нанести `patches/0001-core-integration.diff` (git apply --3way при частичных конфликтах)
   (правки только в `WorldSession.h/.cpp`, ~80 строк).
2. Скопировать `src/bot/*` в `src/server/scripts/Custom/playerbots/`
   (`AddSC_playerbots()` в `custom_script_loader.cpp` уже входит в diff).
3. Добавить ключи из `conf/playerbots.conf.dist` в `worldserver.conf`.
4. Завести бот-аккаунты и персонажей (см. 02_CORE_PATCH.md → «Бот-аккаунты»).
5. `.playerbot add <имя>` — бот появляется в мире как обычный игрок.

## Роадмап

- **Фаза 0 (этот каркас):** вход/выход, follow, чат-команды, простая ротация.
- **Фаза 1:** плавное движение, лут, квесты, гильдии.
- **Фаза 1.5 (готово v2–v5):** movement, классовое знание из спелбукка, ротационный движок (GCD/каст/Dot/burst), авто-шмот, босс-механики Murder Row.
- **Фаза 2:** данжи как группа (танк/хил/дпс стратегии), БГ, rndbots-население.
- **Фаза 3:** «Режим приключения» — компаньоны поверх той же базы (персональные
  боты с профилями поведения). Обсуждено: делается ПОСЛЕ основных ботов.
## Состав

| Путь | Назначение |
|---|---|
| `docs/01_ARCHITECTURE.md` | Устройство входа игрока в master, точки врезки патча, дизайн AI, роадмап |
| `docs/02_CORE_PATCH.md` | Перечень правок ядра + протокол компиляционной проверки |
| `docs/03_WINDOWS_VS2022.md` | Сборка на Windows (VS 2022, x64 Native Tools prompt) |
| `docs/04_V2_MOVEMENT.md` | v2: движение/follow |
| `docs/05_KNOWLEDGE.md` | v3: классовое знание, SQL-таблицы |
| `docs/06_V4_ROTATION_GEAR.md` | v4: ротационный движок, авто-экипировка, roadmap |
| `docs/07_BOSSES_MIDNIGHT.md` | v5: босс-механики, таблица правил |
| `docs/08_CREATE_DUMMY_LOG.md` | v6: создание персонажа, бой с манекеном, формат лога |
| `patches/0001-core-integration.diff` | Реальный git-патч ядра master |
| `sql/` | Схемы world-таблиц (`playerbots_rotation`, `playerbots_combat_spells`, `playerbots_class_knowledge`, `playerbots_hero_talents`) |
| `src/bot/DummyLog.*` | Рекордер лога боя: ScriptMgr-хуки урона/кастов, поллинг аур |
| `src/bot/PlayerbotAI.*` + `Knowledge.h` | AI бота: follow, бой, ротация, авто-шмот |
| `src/bot/cs_playerbots.cpp` | Команда `.playerbots` + WorldScript-тик |
| `conf/playerbots.conf.dist` | Настройки (копировать в worldserver.conf) |
| `tools/apply_windows.cmd` | Патч ядра + копия модуля, **без сборки** (x64 Native Tools prompt, скрипт только ASCII!) |
| `tools/build_windows.cmd` | Сборка после всех правок: cmake reconfigure + game → scripts → worldserver |


