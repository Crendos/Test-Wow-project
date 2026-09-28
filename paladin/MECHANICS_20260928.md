# Механики v9 (28.09.2026) — Свет титанов, Великий крестоносец, Резонанс света

Документ описывает **что именно и почему** изменено в ревизии `PAL_REV9_20260928`
(пак `paladin/`, вставка в `spell_paladin.cpp`), какие факты ядра проверены по исходникам
и как это проверять в игре. Пользовательская инструкция — `INSTALL.md`, §14.

Ревизия ядра, по которой всё сверялось: TrinityCore master `a96d89772a96d275f2d40d2aa27e4a5e0252b04a`
(«Core: Updated allowed build to 12.1.0.69933»).

---

## 0. Что трогает v9

| Файл | Что изменено |
|---|---|
| `paladin/spell_paladin_class_fixes_2.cpp` | класс `spell_pal_light_of_the_titans_ex` переписан + хелпер `Ex2FixTitansHotAmount` (маркер `PAL_TITANS_FIX_20260928`) |
| `paladin/spell_paladin_class_fixes_6.cpp` | помощники `Ex6ResonanceTickSpell` / `Ex6NearestEnemy` / `ScheduleDivineResonanceTicks`, ветка Защиты в Звоне, аура 386730 (маркер `PAL_RESONANCE_15S_20260928`) |
| `paladin/spell_paladin_class_fixes_11.cpp` | новый класс `spell_pal_grand_crusader_reset_ex` + регистрация (маркер `PAL_CRUSADER_RESET_20260928`) |
| `paladin/paladin_class_fixes_11.sql` | привязки `spell_script_names`: `85043` и `85416` → `spell_pal_grand_crusader_reset_ex` |
| `paladin/windows/step1_scripts.ps1`, `check_all.ps1` | ревизия `PAL_REV9_20260928`, новые проверки `[3g]` и «Шаг 1.1: v9 …» |

Правка ядра (`core_patch/0002-…`, шаг 0b) не менялась. Остальные SQL-файлы не менялись.

---

## 1. Проверенные факты API (по исходникам ядра)

Чтобы не угадывать, сверялся код ядра в ревизии выше:

* `CastSpellExtraArgs` (`Spells/SpellDefines.h:520+`): есть `AddSpellMod(SpellValueMod, int32)`,
  `AddSpellMod(SpellValueModFloat, SpellEffectValue)`, `SetTriggeringSpell(Spell const*)`,
  `SetTriggeringAura(AuraEffect const*)`.
* `Spell::SetSpellValue` (`Spell.cpp:8729+`): `SPELLVALUE_BASE_POINTn` → `m_spellValue->EffectBasePoints[n]`,
  `SPELLVALUE_DURATION` → `m_spellValue->Duration`; применяется к ауре в `Spell.cpp:3208`
  (`hitInfo.AuraDuration = *m_spellValue->Duration`) — то есть длительность можно задать **в касте**.
* Хук `AfterHit` (`Spell.cpp:3054`, вызов внутри `TargetInfo::DoTargetSpellHit`) идёт **после**
  применения эффектов, до крита/оверхела: `SpellScript::GetHitHeal()` = `spell->m_healing`
  (базовое лечение эффекта, `SpellScript.cpp:627`).
* `AuraEffect` (`Spells/Auras/SpellAuraEffects.h`): `GetPeriod()`, `GetAmountAsInt()`,
  `SetAmount(SpellEffectValue)` (сбрасывает `m_canBeRecalculated`), `SetPeriodicTimer(int32)`.
* `Aura` (`Spells/Auras/SpellAuras.h`): `GetDuration/GetMaxDuration/SetDuration/SetMaxDuration`,
  `GetAuraEffects()` — диапазон для range-for (идиома ядра, напр. `spell_shaman.cpp:1118`).
* `Unit` (`Entities/Unit/Unit.h`): `HealInfo(Unit* healer, Unit* target, uint32 heal, SpellInfo const*, SpellSchoolMask)`,
  `HealBySpell(HealInfo&, bool critical)`, `SpellHistory* GetSpellHistory()`,
  `ResetCooldown(uint32, bool update)` и `RestoreCharge(uint32 chargeCategoryId)`
  (`Spells/SpellHistory.h:149/186`), `SpellInfo::ChargeCategoryId` (`Spells/SpellInfo.h:420`).
* Шаблоны поиска цели: `Trinity::AnyUnfriendlyUnitInObjectRangeCheck` / `Trinity::UnitListSearcher` /
  `Cell::VisitAllObjects` (`Grids/Notifiers/GridNotifiers.h:561+`) — тот же идиом, что уже был в паке.
* Хуки AuraScript и их **сигнатуры** (`Spells/SpellScript.h`):
  * `OnEffectApply`/`AfterEffectApply` → `void(AuraEffect const*, AuraEffectHandleModes)`;
  * `OnEffectPeriodic` → `void(AuraEffect const*)`;
  * `OnProc` (`AuraProcFn`) → `void(ProcEventInfo&)` — **без** `AuraEffect*` (важно: старая
    сигнатура `(AuraEffect*, ProcEventInfo&)` в этой ревизии уже не компилируется — статический
    `static_assert` внутри `AuraProcHandler`);
  * есть служебные маркеры: `SPELL_AURA_ANY` (`SpellScript.h:61`), `EFFECT_ALL` = 255,
    `EFFECT_FIRST_FOUND` = 254 (`Miscellaneous/SharedDefines.h:71`), `AURA_EFFECT_HANDLE_REAL_OR_REAPPLY_MASK`;
  * `spell_script_names` читается из WorldDatabase (`Globals/ObjectMgr.cpp:5978`),
    UNIQUE-ключ `(spell_id, ScriptName)` — на одно заклинание можно вешать несколько скриптов.

Локальная проверка кода: 4 новых фрагмента (часть 2, помощники и аура резонанса из части 6,
класс Крестоносца из части 11) собраны `g++ -std=c++20 -Wall -Wextra -Werror` на стабах с
**теми же сигнатурами хуков и `static_assert`**, что в ядре — ошибки сигнатур ловятся на месте.

---

## 2. Свет титанов (378405) → ХоТ 378412

**Было.** `GetHitHeal() × pct / 5` шло в `SPELLVALUE_BASE_POINT0` при касте 378412 и больше
ничего: если в данных у 378412 нет периодического лечения (в этой сборке именно так), аура
висела «пустой» — ноль лечения, ровно то, что вы видели.

**Стало.**

1. Процент читается из живой ауры таланта 378405: E0 (по умолчанию 40), E1 (бонус, по умолчанию 200).
   Бонус применяется только если Торжество скастовано **по себе** и на кастере есть
   `SPELL_AURA_PERIODIC_DAMAGE`/`SPELL_AURA_PERIODIC_LEECH` (условие из тултипа).
2. `total = CalculatePct(GetHitHeal(), pct)` — 40% (или больше с бонусом) от лечения Торжества.
3. Каст 378412 идёт с `SPELLVALUE_BASE_POINT0 = total/5` (красиво в клиенте, если данные тикают).
4. Через 0,25 с (каст исполняется на следующем тике мира) хелпер `Ex2FixTitansHotAmount` находит
   ауру 378412 на цели и, если у неё есть эффект `SPELL_AURA_PERIODIC_HEAL`, выставляет
   `amount = total / (длительность / период)` и `SetPeriodicTimer(период)` — сумма тиков равна
   ровно `total`, сколько бы тиков в данных ни было.
5. Если периодики в данных нет — дотикиваем сами: 5 прямых `HealInfo` + `HealBySpell` по
   `total/5` каждые 2 с (в лог боя идёт заклинание 378412). Пока аура тикает сама, «ручные»
   тики не срабатывают (проверка наличия периодики в теле таймера).

Нюанс: база — `GetHitHeal()` (до крита/оверхела). Это соответствует «40% от лечения Торжества»
без учёта крита; по WCL-референсу (64 Торжества → 274 тика, ≈ 4,3 тика на каст) расхождение
в пределах естественного оверхила/крита.

---

## 3. Великий крестоносец (85043) → КД Щита мстителя (31935)

**Почему не работало.** В ядре класс `spell_pal_grand_crusader` есть и зарегистрирован
(`spell_paladin.cpp:1913`), но его единственный «доходный» хук:

```cpp
OnEffectProc += AuraEffectProcFn(spell_pal_grand_crusader::HandleEffectProc, EFFECT_0, SPELL_AURA_PROC_TRIGGER_SPELL);
```

То есть сброс КД происходит **только** если эффект №0 ауры имеет ровно тип
`SPELL_AURA_PROC_TRIGGER_SPELL`. Если в данных сборки тип другой (или строки привязки в
`spell_script_names` нет) — хук не вызывается, и КД не сбрасывается, хотя сам баф прока 85416
на игрока приходит (его выдаёт прок-движок, а не скрипт).

**Стало** (часть 11, `spell_pal_grand_crusader_reset_ex`): ловим оба места, без привязки к типам:

```cpp
AfterEffectApply += AuraEffectApplyFn(..., EFFECT_FIRST_FOUND, SPELL_AURA_ANY, AURA_EFFECT_HANDLE_REAL_OR_REAPPLY_MASK); // баф 85416
OnProc           += AuraProcFn(...);                                                                                   // пассивка 85043
```

Обработчик: `Player*` → `SpellHistory::ResetCooldown(31935, true)` + `RestoreCharge(ChargeCategoryId)`
(ровно как в рабочем `Ex11ReflectionOfRadiance`, часть 11) и одна информационная строка в лог за
запуск сервера. Привязки — в `paladin_class_fixes_11.sql` (85043 и 85416 → наш скрипт).
Так как UNIQUE-ключ таблицы — `(spell_id, ScriptName)`, ядровый скрипт при этом не ломается:
если он у вас привязан и типы совпадают, сброс просто произойдёт дважды (идемпотентно).

Не сделано (по желанию, вне текущего списка): ветка таланта «+Сила» (393019/393024) и сброс КД
Правосудия (204023) — см. строки 146/153/239 в `PALADIN_AUDIT.md`.

---

## 4. Резонанс света (386730, Защита) → 15 с и три тика

**Почему не работало.** Длительность выставлялась *после* `CastSpell`, а `CastSpell` в ядре
только **заказывает** каст: сам каст исполняется на следующем тике мира (`WorldObject::CastSpell`
→ очередь `m_Events`). К моменту `ForceAuraDuration` ауры ещё нет, поэтому оставалась длительность
из данных (10 с). Плюс в данных у 386730 нет своей периодики — отсюда «один щит вместо трёх».

**Стало.**

* `AddSpellMod(SPELLVALUE_DURATION, 15000)` **в том же `CastSpellExtraArgs`** — аура создаётся
  сразу 15-секундной, клиент с первого кадра видит 15 с (`Spell.cpp:3208`);
* прежний `ForceAuraDuration` остался страховкой, а аура-скрипт после наложения любого эффекта
  (`AfterEffectApply` + `EFFECT_FIRST_FOUND` + `SPELL_AURA_ANY` + `…REAL_OR_REAPPLY_MASK`)
  дополнительно поднимает `GetMaxDuration()/GetDuration()` до 15 с — на случай, если ауру выдал
  не наш каст (например, ядро/другая ветка);
* три тика (5/10/15 с) ведёт `ScheduleDivineResonanceTicks`, запускаемый из Звона: каждый тик —
  `Щит мстителя` (Защита) или `Святая вспышка` (Свет) по ближайшему врагу в 30 м с проверкой
  `IsValidAttackTarget` + `IsWithinLOSInMap`; каст идёт с `SetTriggeringAura(эффект аур 386730)`,
  чтобы пробудить нужные прок-цепочки;
* на каждый тик проверяется, что кастер жив и на нём всё ещё висит 386730 (нет Звона — нет щитов);
* собственная периодика ауры глушится: `OnEffectPeriodic` (`EFFECT_ALL` + `SPELL_AURA_ANY`) →
  `PreventDefaultAction()`, иначе щиты посчитались бы дважды.

---

## 5. Совместимость и откат

* Все правки внутри `namespace { … }` файлов пака — повторный запуск `step1_scripts.bat`
  безопасен: скрипт откатывает `spell_paladin.cpp` до чистого ядрового и вставляет всё заново.
* Убрать v9 = запустить `step1_scripts.bat` с **предыдущей** версией папки `paladin`
  (или из ядра: `git checkout -- src/server/scripts/Spells/spell_paladin.cpp` и пересборка).
  SQL откатывать не обязательно: привязки 85043/85416 без скрипта в ядре игнорируются
  (в логе будет строка про неизвестное имя скрипта — одна, при старте).
* Ядровые хуки, использованные в v9 (`AfterEffectApply`, `OnProc`, `OnEffectPeriodic`,
  `SPELL_AURA_ANY`, `EFFECT_FIRST_FOUND`) — публичный API `SpellScript.h`, при обновлении ядра
  ломаются только при смене сигнатур; тогда пересобрать и посмотреть `step1_scripts` вывод `[3e]/[3g]`.
