# v6: создание персонажа, бой с манекеном, лог боя

Три команды без входа в клиент:

1. `.playerbots create` — сервер создаёт персонажа на указанном аккаунте по
   критериям (класс/спек/геро-дерево/уровень/предметы/ник).
2. `.playerbots dummy start|stop` — бот идёт к точке (опционально) и бьёт манекен,
   ведя полный лог боя.
3. `DummyLog` — файл лога: ауры/бафы, касты, урон (включая проки), хил, итог.

Код: `src/bot/PlayerbotMgr.cpp` (CreateCharacter), `src/bot/PlayerbotAI.*`
(режим dummy), `src/bot/DummyLog.*` (рекордер + ScriptMgr-хуки),
`src/bot/cs_playerbots.cpp` (команды). SQL: `sql/world_playerbots_hero_talents.sql`
(загрузить в **World DB**).

---

## 1. `.playerbots create`

```
.playerbots create <account> class=<класс> [race=N] [side=ally|horde] [gender=m|f] [level=N]
                   [spec=dps|heal|tank|имя|ID] [hero=<дерево>]
                   [item=ID[,ID...]] [name=Имя]
```

Примеры:

```
.playerbots create 9001 class=paladin level=80 spec=dps hero=templar
.playerbots create 9002 class=paladin level=80 spec=ret item=2000,item=2001 name=Qwk
.playerbots create 9003 class=paladin level=90 spec=dps hero=templar side=horde
```

| Параметр | Значение |
|---|---|
| `<account>` | ID аккаунта в **auth**. Персонаж привязывается к нему. Обычно — бот-диапазон `Playerbots.FreeAccountsStart/End` (9000…9499); вне диапазона команда предупредит. |
| `class=` | `paladin`/`pala`/`2`… (warrior, hunter, rogue, priest, dk, shaman, mage, warlock, monk, druid, dh, evoker) или число. |
| `race=` | `RACE_*` число. Пропущено → первая playable-раса, у которой есть комбинация с классом (для паладина будет human=1). |
| `side=` | `ally`/`alliance`/`a`/`альянс` либо `horde`/`h`/`орда` — выбор **стороны**: авто-подбор первой playable-расы стороны, валидной для класса (паладин: ally→human=1, horde→bloodelf=10). Нельзя вместе с `race=` (парсер откажет). |
| `gender=` | `m`/`f`/`0`/`1` (по умолчанию `m`). |
| `level=` | 1…`MaxPlayerLevel` (под кап сервера). **Уровень ставится через `GiveLevel` ПОСЛЕ выбора спека** — спеллы спека выучиваются автоматически. |
| `spec=` | пусто → первый DPS-спек класса (паладин → Retribution 70); `dps`/`dd`, `heal`, `tank` (роль из ChrSpecialization.Role); имя спека по-английски (`retribution`, `holy`…); либо точный Spell/Spec ID (`70`). |
| `hero=` | ключ дерева героических талантов. Сейчас см. §2. |
| `item=` | ID предметов через запятую — кладутся в сумки лучшими слотами (`StoreNewItemInBestSlots`). Надеть: после входа бота `.playerbots equip <имя>`. |
| `name=` | явный ник (проходит те же проверки, что и рандомный). Пусто → **случайный ник из 3–4 латинских букв** (первая заглавная), перебор до 80 свободных вариантов. |

Что реально происходит (паттерн `CharacterHandler::HandlePlayerCreate`):

- синхронная проверка аккаунта в auth (`LoginDatabase.Query`);
- временная фейковая `WorldSession` аккаунта (`sock=nullptr`, `MarkAsPlayerBot`);
- `Player::Create` с race/class/gender; **кастомизация не передаётся** —
  пустой список валиден (на CREATE `ValidateAppearance` не вызывается),
  поэтому внешний вид будет дефолтным для расы;
- спек → `SetPrimarySpecialization` + `SetActiveTalentGroup(OrderIndex)`;
- `GiveLevel(level)` → `LearnSpecializationSpells` (спеллы спека ≤ уровня);
- автоталанты: жадная сборка конфига → `CreateTraitConfig` → **`ApplyTraitConfig(id, true)`**
  (`LearnSpell` талант-спеллов всех нод — сам TC записанный конфиг без клиентского
  коммита НЕ применяет) → сохраняется в `character_trait_config`/`character_trait_entry`;
- `hero=` → `LearnSpell` каждого spell_id дерева;
- `item=` → `StoreNewItemInBestSlots`;
- **без** `AT_LOGIN_FIRST` (бот не проходит «первый вход»-флоу);
- `SaveToDB(loginTrans, charTrans, create=true)` + **синхронный**
  `DirectCommitTransaction` в обе БД (команда ждёт результата);
- `AddCharacterCacheEntry` (ник виден в кэше/поиск) + `OnPlayerCreate`.

Ограничения (осознанно): не проверяются лимиты персонажей на аккаунт и
disabled race/class маски; кастомизация лица — дефолтная; таланты/талант-поинты
не распределяются (ротации это не мешает — знание строится из спелбукка).

Дальше: `.playerbots add <Имя>` — бот заходит тем же аккаунтом (это уже
существующий путь, `docs/01`), `.playerbots equip <Имя>` — экипировка.

---

## 2. `hero=` и геро-таланты

Ядро TrinityCore **не реализует** систему героических талантов (HeroTalents/Trait
деревья для Midnight в core отсутствуют). Поэтому `hero=templar` работает так:

- таблица **World DB** `playerbots_hero_talents(class, tree, spell_id)` —
  загрузка в `sql/world_playerbots_hero_talents.sql`;
- при создании все spell_id дерева вызываются `LearnSpell` — они попадают в
  знания AI (`BuildKnowledgeFromSpellbook`), участвуют в ротации и учитываются
  как обычные спеллы/кулдауны;
- стартовый набор: **Hammer of Light = 429826** (Templar keystone-актива).
  Остальные спеллы дерева дописывай сам: нашёл spell_id на wowhead → INSERT
  той же строкой SQL (пример в самом файле);
- эффекты «фишек» дерева (вызов Empyrean Hammers и т.п.) ожидать рано — это
  spell-скрипты ядра, в TC master их нет. Роль `hero=` сейчас: правильный профиль
  персонажа + расширение ротации.

---

## 3. `.playerbots dummy`

```
.playerbots dummy start <бот> [entry манекена] [x y z] [sweep]
.playerbots dummy stop <бот>
```

Приоритет выбора цели в `start`:

1. **твой выделенный юнит** (`.target`/кликом) — должен быть Creature рядом с ботом;
2. `entry=N` — `FindNearestCreature(entry, 300)` от позиции бота;
3. авто-поиск: `creature_template.name LIKE '%Dummy%' | '%Манекен%' | '%Training%'`
   (первый найденный в 300 ярдах от бота).

`[x y z]` — опциональная точка: сперва бот подбегает к ней (`MovePoint`, лимит
60 сек), затем держит дистанцию у манекена и дрится. Пустая точка — сразу бой.

Поведение: бот атакует (`Attack`), включает обычную боевую ротацию
(`DoCombatAI`: бафы, спеллы по кулдауну, дебафы/DoT на цели, defensive при
НПХ), при отдалении — `MoveFollow`. Режим имеет высший приоритет в `Update()`
(обходит follow/plane). Манекен умер/пропал → режим сам завершается.

`stop` → снимает режим, пишет `SUMMARY` и печатает путь файла.

### QA-sweep (`sweep`) — только для QA-ботов (см. §4)

Боты позиционируются как **QA для вылавливания багов класса** — им важна
полнота покрытия, а не красота ротации. Флаг `sweep` включает одиночный прогон:

- берётся ВСЁ боевое знание (`Damage`/`DoT`/`Debuff` + `SelfBuff`/`Defensive`/`Interrupt` с v6.4, приоритетный порядок; `Heal`/`Dispel` вне свипа — на полном HP манекена их гейты дали бы шум отказов, а не баг);
- **только QA-роль**: игровому боту `sweep` не включается (команда откажет с подсказкой про `Playerbots.QAAccountsStart/End`);
- каждый спелл пытается кастовать **один раз** независимо от условий
  (без HP/бёрст-гейтов): в лог падает `SWEEP attempt … seq=i/M`,
  при кулдауне — `SWEEP skip (cooldown)`, в конце — `SWEEP complete ok=… fail=…`;
- успех подтверждается строкой `CAST` (хук `OnSpellCast`), отказ — `CAST_FAIL`;
- после прогона бот переходит на обычную ротацию на остаток боя.

Что смотреть в логе как QA: `CAST_FAIL` с ресурсом (`NO_POWER(ресурс)` —
нехватка Holy Power у спендеров), `E<номер>` = неизвестный код отказа ядра
(кандидат на баг/покрытие), спелл `CAST`, но без последующих строк `DMG`
(каст прошёл, урона нет — возможный баг класса), `SUMMARY.cast_fails`.

Предварительно бот должен быть онлайн: `.playerbots add <Имя>`, дальше
`.playerbots summon <Имя>` (мгновенно к вам, даже с другой карты) либо
`.playerbots followme <Имя>` довести пешком (или `dummy start … x y z`).

---

## 4. Роли: QA-боты vs игровые боты

Роль определяется **диапазоном аккаунта** (персистентно, без БД):

| | Игровой (9000…9499 по умолчанию) | QA (`Playerbots.QAAccountsStart/End`, 9500…9599) |
|---|---|---|
| Назначение | обычные боты: follow, ростер, бои как игроки | вылавливание багов класса |
| `dummy start … sweep` | ❌ (команда откажет) | ✅ прогон всех боевых спеллов по разу |
| Отказ каста (`!= SPELL_CAST_OK`) | полный GCD, молча (поведение v4/v5) | `CAST_FAIL` в лог + пауза 500мс и следующий спелл по приоритету |
| Чёрный список фейлов | — | намеренно пуст: QA видят повторные попытки |
| `.playerbots list` | `Name` | `Name  [QA]` |
| `.playerbots create` | печатает «Роль: игровой» | печатает «Роль: QA …» |

Создание QA-персонажа:

```
# аккаунт 9501 при отсутствии создаётся автоматически (login PB9501, см. ниже)
.playerbots create 9501 class=paladin level=80 spec=dps hero=templar
# → «Роль: QA (sweep, CAST_FAIL, быстрые ретраи фейлов)»
.playerbots add <Имя>
.playerbots dummy start <Имя> sweep
```

#### Спеки и геро-деревья (что поддерживается)

`spec=` работает для **любого** класса: ролью (`dps|heal|tank`), именем спека
на en (напр. `protection`) или числовым ChrSpecialization ID. Паладин
(Midnight 12.1), ключи `hero=`:

| Спека | `spec=` | Деревья `hero=` |
|---|---|---|
| Retribution (урон) | `dps` | `templar`, `herald` |
| Holy (хил) | `heal` | `herald`, `lightsmith` |
| Protection (танк) | `tank` | `templar`, `lightsmith` |

`hero=` принимает **несколько веток через запятую** (учит union обоих деревьев
спека) и `all` (все ветки класса):

```
# обе ветки спека сразу:
.playerbots create 9501 class=paladin level=90 spec=dps  hero=templar,herald
.playerbots create 9502 class=paladin level=90 spec=heal  hero=herald,lightsmith
.playerbots create 9503 class=paladin level=90 spec=tank  hero=templar,lightsmith

# или дозаучить вторую ветку на УЖЕ созданном боте (пересборка знаний AI автоматом):
.playerbots hero Xkq herald
.playerbots hero Ykq all
```

Опечатка в `hero=` → в ошибке перечисляются доступные для класса деревья.
Спеллы дерева, отсутствующие в DBC клиента, пропускаются с пометкой в
«Замечания». Деревья других классов — INSERT-ы в
`sql/world_playerbots_hero_talents.sql` (класс-привязку сверять на wowhead:
тип Talent + перечень классов).

#### Как создаются бот-аккаунты

Если аккаунта с указанным **числовым id** нет в auth-БД, `.playerbots create`
**создаёт его автоматически** (login `PB<id>`, пароль не задан — боты в него
не входят; в «Замечаниях» печатается `аккаунт N создан автоматически`).
Роль бота (QA/игровой) определяется id-диапазоном, так что для обычной
работы ничего настраивать не нужно.

Ручная процедура нужна только если хотите, чтобы аккаунт был полноценным
(с паролем для входа). Команда `.account create` делает id автоматическим,
поэтому диапазон задаётся через AUTO_INCREMENT (один раз):

```sql
-- mysql -u trinity -p auth   (СНАЧАЛА: SELECT MAX(id) FROM account; — должен быть < 9500)
ALTER TABLE account AUTO_INCREMENT = 9500;
```

```text
-- worldserver-консоль/GM (пароль любой, боты в него не входят):
.account create qa9500 AnyPass1     → id 9500
.account create qa9501 AnyPass1     → id 9501  ← попадает в QAAccountsStart/End
```

```sql
-- проверка:
SELECT id, username FROM account WHERE id BETWEEN 9500 AND 9599;
```

Игровые аккаунты (9000…9499) — так же, но `AUTO_INCREMENT = 9000`
(значение должно быть больше MAX(id); порядок: сначала меньший диапазон,
затем `ALTER` на больший). Если свободные id в диапазоне недоступны
(сервер не пустой) — либо сдвиньте `Playerbots.QAAccountsStart/End`
в worldserver.conf под свободный хвост, либо вставляйте строку в
`account` вручную по схеме своей auth-БД.

**Быстрый тест:** `.playerbots create <любой существующий_id> …` работает и
с обычными аккаунтами (роль «игровой», если id не в QA-диапазоне) — либо
просто указывай любой свободный id: недостающий аккаунт создастся сам.

Игровой и QA-бот могут драться с одним манекеном параллельно — у каждого
свой лог; сравнивая строки `CAST`/`CAST_FAIL`/`DMG` двух файлов, видно,
чем «игровая» ротация деградирует против полного QA-покрытия.

## 5. Лог боя (DummyLog)

Ключ конфига: `Playerbots.DummyLogDir` (по умолчанию `PlayerbotsLogs/`,
относительно рабочего каталога worldserver; папка создаётся сама).
Файл: `<Bot>_<YYYYMMDD-HHMMSS>.log`.

**Рядом с логом на `dummy stop` автоматически создаётся QA-отчёт**
`<Bot>_<YYYYMMDD-HHMMSS>.qa.txt` — «анализ на некорректную работу/баги»
(см. §4.1). Он всегда пишется, даже если аномалий нет (`FINDINGS 0`), и
содержит только выводы, а не сырые строки — сырые данные остаются в `.log`.

#### §5.1 Что ищет QA-отчёт (`.qa.txt`)

| Метка | Условие | Что это значит |
|---|---|---|
| `[BUG] unknown_cast_result` | код отказа `E<номер>` (не из списка имён) | непокрытый случай `SpellCastResult` — разобрать, кандидат на баг/дыру покрытия |
| `[WARN] resource_starved` | уронный спелл (спендер) отклонён `NO_POWER` | нет Holy Power/ресурса на момент каста |
| `[SUSPECT] cast_without_damage` | уронный спелл скастован ≥1 раз, ни одной строки `DMG` с его spellId | каст «в никуда»: баг класса, прок-замена или особенность — сверить с `.log` |
| `[BUG] zero_total_damage` | `casts>0`, `total_dealt=0` | бой не нанёс урона вовсе |
| `[INFO] repeated_failure` | ≥5 отказов одного спелла с известным кодом | систематический отказ |
| `[SUSPECT] aura_not_applied` | спелл скастован, эффект `APPLY_AURA` есть в DBC, а аура не появлялась ни у бота, ни у цели | **слой 1 (авто):** DBC обещает ауру — её нет. Баг или аура короче поллинга (300 мс) — сверить `.log` |
| `[SUSPECT] power_not_spent` | цена > 0 по DBC, суммарное списание 0 (строки `PWR`) | **слой 1 (авто):** бесплатный прок или баг цены |
| `[SUSPECT] mechanic_power_cost` | строка `playerbots_mechanics power_cost` не сошлась с `PWR` | **слой 2 (только QA):** цена не как в знании |
| `[BUG] mechanic_missing` | строка `proc_after`/`aura_after`: окно истекло, арг кастовался, но события в окне не было (для `proc_after` — после триггера пробовали и отказали) | **слой 2 (только QA):** механика не реализована/не сработала |
| `[INFO] mechanic_skipped / mechanic_recheck / mechanic_window_active` | триггер/арг не кастовали, порядок sweep не совпал или окно не истекло | не доказательство — нужен повторный прогон |
| `[INFO] mechanics_coverage` | для класса QA-бота нет строк в `playerbots_mechanics` | пробел покрытия: после починки класса добавь знания (§5.2) |
| `[INFO] SWEEP …` | был QA-прогон | итог покрытия `ok/fail/total` |
| `[STAT] …` | всегда | duration, total_dealt, casts, cast_fails |

Шапка отчёта: бот, цель/entry, время, `reason` и ссылка на сессионный `.log`
(там же полные строки `CAST`/`CAST_FAIL`/`DMG` с таймкодами для каждого пункта).

**Атрибуция по талантам (только QA-отчёты).** Каждая находка со spell ID
дополняется суффиксом ` | талант: <Имя> (<ID>) [hero: <дерево>]` — «под каким
талантом это идёт», если талант влияет на спелл. Источники: (а) DBC
`TraitDefinition` — талант-нода **даёт** спелл (SpellID/VisibleSpellID),
**перекрывает** базовый (`OverridesSpellID`) либо его эффект **триггерит**
другой спелл (`EffectTriggerSpell`); (б) таблица `playerbots_hero_talents`
(`[hero: templar]` и т.п.). Если талант не влияет — суффикса нет. Примеры:

```
[SUSPECT] cast_without_damage spell=429826 name="Hammer of Light" casts=3 … | талант: Hammer of Light (429826) [hero: templar]
[BUG] mechanic_missing check=aura_after trigger=431413 expected_spell=431413 … | талант: Sun Sear (431413) [hero: herald]
```

Формат: `T=<сек с начала> <ТИП> key=value …` (имена в кавычках).

| Тип | Источник | Что даёт |
|---|---|---|
| `AURA + / - / ~` | поллинг `GetAppliedAuras()` каждые 300 мс (бот и цель) | появление/снятие/смена стаков каждой ауры: `spell=`, `name=`, `who=self\|target`, `buff\|debuff`, `stacks=`, `dur_ms=`, `caster=`. Бафы, висевшие до старта, тоже попадают (снапшот при старте). |
| `CAST` | `PlayerScript::OnSpellCast` (`Spell::_cast`) | каждый **успешный** каст бота: spell id/имя, цель. |
| `CAST_FAIL` | возврат `WorldObject::CastSpell` (`SpellCastResult`) | **отклонённый** каст: `result=N (ИМЯ)` — `NO_POWER`, `NOT_READY`, `OUT_OF_RANGE`, `STUNNED`… Неизвестные коды — `E<N>` (возможный баг/непокрытый случай). |
| `PWR` | `PlayerbotAI::CastSpellAt` (после успешного каста, если цена > 0) | фактическое списание ресурса: `spell=`, `name=`, `spent=` (0 = бесплатный прок или баг) — данные для проверок `power_not_spent`/`power_cost` (§5.2). |
| `SWEEP …` | режим `sweep` | `attempt`/`skip (cooldown)`/`complete` — отчёт QA-прогона спеллов. |
| `DMG src=spell` | `UnitScript::ModifySpellDamageTaken` | урон способностью: spell id/имя, `amount`, жертва. |
| `DMG src=melee` | `UnitScript::ModifyMeleeDamage` | белый урон. |
| `DMG src=dot` | `UnitScript::ModifyPeriodicDamageAurasTick` | тик периодики (хук не несёт spell id — смотри параллельные `AURA` строки этой же ауры). |
| `HEAL` | `UnitScript::OnHeal` | лечение ботом. |
| `SUMMARY` | `stop` | `duration_s`, `total_dealt` (авторитетный итог из `OnDamage`), `casts`, **`cast_fails`**, `reason`. |
| `QA_REPORT` | `stop` | путь к записанному рядом QA-отчёту `<…>.qa.txt` (анализ сессии, §5.1). |

**Проки.** Отдельного ScriptMgr-хука на проки нет. Проки видны в логе двумя
способами: (а) строки `DMG src=spell` с spell id самого прока (не того, что
жмякал бот), (б) `AURA +` на боте/цели от прок-баффа в момент срабатывания.
Сверяй по `T=`-таймкодам.

Записи идут в файл сразу (flush после каждой строки) — можно читать лог во
время боя. Пока бот онлайн с активным режимом — ровно один открытый лог на бота.

#### §5.2 Два слоя знаний о механиках (QA-only)

Отчёты `.qa.txt` проверяют не только общие инварианты (§5.1), но и знания
«как должна работать способность/талант». Два слоя:

**Слой 1 — авто-ожидания из DBC (никаких таблиц).** Для каждого скастованного
спелла сверяем обещания `SpellInfo` с событиями сессии:
эффект `APPLY_AURA` → аура обязана появиться (иначе `aura_not_applied`);
цена > 0 → ресурс реально списан (строки `PWR`, иначе `power_not_spent`);
эффект урона → уже существующая проверка `cast_without_damage`.

**Слой 2 — таблица `playerbots_mechanics` (ручные знания).**
Схема/тип/семантика колонок — в шапке `sql/world_playerbots_mechanics.sql`.
Проверки: `proc_after`, `aura_after`, `power_cost` (см. таблицу §5.1).
Исполнение: при закрытии сессии по таймлайну событий (`C/D/F/A/P`),
окно `window_ms` обязано целиком укладываться в длительность сессии.

**Гейт: оба слоя в `.qa.txt`, слой 2 читает только QA-роль.**
Обычные игровые боты таблицу механик не читают и не применяют —
гейт `IsQAAccount` в `PlayerbotDummyLog::WriteQaReport`
(слой 1 — только отчёт, поведение ботов не меняет вовсе).

**Как расширять на другие классы:** починил класс → вставил строки своего
`class_id` в `sql/world_playerbots_mechanics.sql` (и при необходимости
`world_playerbots_class_knowledge.sql`) → `INSERT IGNORE` в мировую БД →
QA-боты этого класса начинают ловить его баги. Класс без строк виден в
отчёте как `[INFO] mechanics_coverage class=N` — это план работ, а не ошибка.
Сид: паладин (class_id=2) — Templar/Herald (см. сам SQL).
Файлы: `sql/world_playerbots_mechanics.sql` (применить вручную в World DB),
код — `src/bot/DummyLog.{h,cpp}` (запись событий + отчёт),
`src/bot/PlayerbotAI.cpp` (`NotePowerSpent` в `CastSpellAt`).

---

## 5. Сквозной пример

```
# 0) разово: выполнить sql/world_playerbots_hero_talents.sql в World DB
# 1) аккаунт 9001 уже существует в auth (диапазон ботов)
.playerbots create 9001 class=paladin level=80 spec=dps hero=templar item=2000,item=2001
# → «Создано: Xkq (guid 123, account 9001)»

.playerbots add Xkq
.playerbots equip Xkq
.playerbots summon Xkq            # телепорт бота к себе (быстро); либо followme — пешком
.playerbots dummy start Xkq        # (или с выделенным манекеном / entry=NNN / x y z)

# ... бой ...

.playerbots dummy stop Xkq
# → «Лог закрыт: PlayerbotsLogs/Xkq_20260929-153000.log»
```

## 7. Новые ключи конфига

| Ключ | По умолчанию | Смысл |
|---|---|---|
| `Playerbots.DummyLogDir` | `PlayerbotsLogs` | папка логов боя (в `worldserver.conf`, рядом с остальными `Playerbots.*`) |
| `Playerbots.QAAccountsStart` / `End` | `9500` / `9599` | диапазон QA-аккаунтов (роль бота: sweep, `CAST_FAIL`, быстрые ретраи) |
