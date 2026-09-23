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


Roadmap v5+: точный GCD с хаст-коррекцией, стат-скоринг шмота по классам,
босс-механики (engine правил + proof-of-concept на одном боссе), роуты/патрули,
whisper-управление, компаньоны.


## Порядок внедрения (кратко, детали — в docs/)

**Windows/VS2022:** готовый скрипт `tools/apply_windows.cmd` (запускать из
«x64 Native Tools Command Prompt for VS 2022») + пошаговая инструкция
`docs/03_WINDOWS_VS2022.md`.

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
| `patches/0001-core-integration.diff` | Реальный git-патч ядра master |
| `sql/` | Схемы world-таблиц (`playerbots_rotation`, `playerbots_combat_spells`, `playerbots_class_knowledge`) |
| `src/bot/PlayerbotMgr.*` | Менеджер ботов: вход/выход, учёт, тик, резолв знания |
| `src/bot/PlayerbotAI.*` + `Knowledge.h` | AI бота: follow, бой, ротация, авто-шмот |
| `src/bot/cs_playerbots.cpp` | Команда `.playerbots` + WorldScript-тик |
| `conf/playerbots.conf.dist` | Настройки (копировать в worldserver.conf) |
| `tools/apply_windows.cmd` | Применение изменений на Windows-машине (x64 Native Tools prompt, скрипт только ASCII!) |


