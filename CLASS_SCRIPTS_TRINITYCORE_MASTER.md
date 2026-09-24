# TrinityCore (master) — где и как устроены скрипты классов

Анализ официального репозитория `TrinityCore/trinitycore`, ветка `master`
(HEAD на момент анализа: `18b50b49` — «Scripts/Spells: Implement paladin talent Expurgation (#31968)»,
т.е. свежие коммиты ветки — это именно скрипты способностей/талантов классов).
Клиент-версия мастера: 12.x (в `sql/old/` уже есть `10.x`, `11.x`, `12.x`).

---

## 1. Главное место: `src/server/scripts/Spells/`

Здесь лежат **C++-скрипты способностей**, разложенные по файлам на класс:

| Файл | Строк | Скриптов (классов) | Префикс имён |
|---|---:|---:|---|
| `spell_priest.cpp`   | 5609 | 137 | `spell_pri_` |
| `spell_druid.cpp`    | 3211 |  99 | `spell_dru_` |
| `spell_shaman.cpp`   | 3578 |  95 | `spell_sha_` |
| `spell_dh.cpp`       | 2918 |  84 | `spell_dh_`  (дьявольский охотник) |
| `spell_warrior.cpp`  | 2423 |  71 | `spell_warr_` |
| `spell_warlock.cpp`  | 1941 |  68 | `spell_warl_` |
| `spell_mage.cpp`     | 2193 |  66 | `spell_mage_` |
| `spell_paladin.cpp`  | 1945 |  56 | `spell_pal_` |
| `spell_dk.cpp`       | 1579 |  53 | `spell_dk_`  (рицарь смерти) |
| `spell_rogue.cpp`    | 1615 |  50 | `spell_rog_` |
| `spell_hunter.cpp`   | 1512 |  50 | `spell_hun_` |
| `spell_evoker.cpp`   |  795 |  22 | `spell_evo_` |
| `spell_monk.cpp`     |  816 |  20 | `spell_monk_` |
| `spell_generic.cpp`  | 5925 | 154 | `spell_gen_` — общее (не привязано к классу) |
| `spell_item.cpp`     | 4971 | 151 | `spell_item` — способности предметов |
| `spell_pet.cpp`      | 1631 |  22 | питомцы/стражи |
| `spell_azerite.cpp`  |  646 |  22 | азеритовые предметы |
| `spell_quest.cpp`    |  343 |   4 | квестовые |

**Итого ~1200 классов-скриптов способностей.** Сами классы-скрипты в каждом файле
расположены **в алфавитном порядке по имени скрипта** (это оговорено в заголовке файла).

Рядом:
- `src/server/scripts/Custom/custom_script_loader.cpp` — стандартное место для **своих** скриптов (`AddCustomScripts()`).
- `doc/HowToScript.txt` — официальная (старая, но полезная) инструкция по скриптингу.

## 2. Структура скрипта класса (пример на воине)

Каждый файл начинается с **enum'а ID способностей** — это и есть те самые ID,
которые вы ищете на WCL/wowhead:

```cpp
// src/server/scripts/Spells/spell_warrior.cpp
enum WarriorSpells
{
    SPELL_WARRIOR_REND                        = 772,
    SPELL_WARRIOR_EXECUTE                     = 20647,
    SPELL_WARRIOR_SHIELD_WALL                 = 871,
    SPELL_WARRIOR_RALLYING_CRY                = 97463,
    ...
    SPELL_WARRIOR_BRUTAL_FINISH_TALENT        = 446085,   // талант
    SPELL_WARRIOR_SURGE_OF_ADRENALINE_TALENT  = 1265359,  // талант
};
```

Дальше — сами скрипты. Два вида:

### `SpellScript` — логика применения спелла

```cpp
// 34428 - Victory Rush
class spell_warr_victory_rush : public SpellScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo
        ({
            SPELL_WARRIOR_VICTORIOUS,
            SPELL_WARRIOR_VICTORY_RUSH_HEAL
        });
    }

    void HandleHeal()
    {
        Unit* caster = GetCaster();
        caster->CastSpell(caster, SPELL_WARRIOR_VICTORY_RUSH_HEAL, true);
        caster->RemoveAurasDueToSpell(SPELL_WARRIOR_VICTORIOUS);
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_warr_victory_rush::HandleHeal);
    }
};
```

### `AuraScript` — проки и логика аур (чаще всего это таланты)

```cpp
// 383344 - Expurgation  (паладинский талант, самый свежий коммит в master!)
class spell_pal_expurgation : public AuraScript
{
    bool Validate(SpellInfo const* /*spellInfo*/) override
    {
        return ValidateSpellInfo({ SPELL_PALADIN_EXPURGATION });
    }

    static void HandleProc(AuraScript const&, AuraEffect const* /*aurEff*/, ProcEventInfo const& eventInfo)
    {
        eventInfo.GetActor()->CastSpell(eventInfo.GetActionTarget(),
            SPELL_PALADIN_EXPURGATION, TRIGGERED_IGNORE_CAST_IN_PROGRESS | TRIGGERED_DONT_REPORT_CAST_ERROR);
    }

    void Register() override
    {
        OnEffectProc += AuraEffectProcFn(spell_pal_expurgation::HandleProc, EFFECT_0, SPELL_AURA_DUMMY);
    }
};
```

В конце файла — функция регистрации всех скриптов файла:

```cpp
void AddSC_warrior_spell_scripts()
{
    RegisterSpellScript(spell_warr_victory_rush);
    RegisterSpellAndAuraScriptPair(spell_warr_thunder_blast, spell_warr_thunder_blast_aura);
    ...
}
```

Загрузчик модуля: `src/server/scripts/Spells/spell_script_loader.cpp` —
объявляет все `AddSC_*_spell_scripts()` и вызывает их из `AddSpellsScripts()`,
который в свою очередь вызывается из генерируемого `ScriptLoader.cpp` (`AddScripts()`).

## 3. Как скрипт привязывается к способностям (ID)

Привязка **спелл-ID ↔ имя скрипта** хранится в БД (world):

```sql
CREATE TABLE `spell_script_names` (
  `spell_id` int NOT NULL,
  `ScriptName` varchar(120) NOT NULL
);
-- отрицательный spell_id = все ранги спелла
```

Цепочка загрузки:

1. Старт сервера → `ObjectMgr::LoadSpellScriptNames()`
   (`src/server/game/Globals/ObjectMgr.cpp`, ~строка 5974) читает таблицу в `_spellScriptsStore`.
2. При касте: `Spell::LoadScripts()` (`src/server/game/Spells/Spell.cpp:8770`)
   → `sScriptMgr->CreateSpellScripts(spellId, ...)` (`src/server/game/Scripting/ScriptMgr.cpp:1454`)
   → ищет скрипты по ID и вызывает в них `Validate()` + `Register()`.
3. Перезагрузка без рестарта: команда `/reload spell_script_names`.

> **Важно:** в репозитории идёт только «dev»-база world (246 таблицы, пара строк
> `spell_script_names`). Полная боевая world-БД с тысячами строк привязок
> распространяется отдельно (скачивается с trinitycore.org). Т.е. полный список
> «какой ID → какой скрипт» в самом git-репо **нет** — он в world-БД.

Новые привязки добавляются через update-файл:
`sql/updates/world/master/YYYY_MM_DD_NN_world.sql`
(свежие в master: `2026_09_20_00_world.sql`, `2026_09_21_03_world.sql`).

## 4. Таланты

В актуальном ритейле (10.x+) система талантов в TC названа **«traits»**:

- движок: `src/server/game/Spells/TraitMgr.cpp` / `TraitMgr.h`
- обработчик пакетов: `src/server/game/Handlers/TraitHandler.cpp`, `src/server/game/Server/Packets/TraitPackets*.cpp`
- данные деревьев талантов — в **DBC** (`TraitDefinitions*`, `TraitRankItems*` и т.п.) — из клиентских файлов,
- конфигурация талантов персонажа — в БД characters: таблица `character_trait_config`
  (guid, spec, traitConfigId, button).

**Сами таланты — обычные спеллы** (есть ID, эффекты описаны в `Spell.dbc`).
Поэтому «заскриптовать талант» = написать `SpellScript`/`AuraScript` на ID спелла-таланта,
точно так же, как на обычную способность. Примеры в файлах классов:

- `spell_warr_surge_of_adrenaline` (1265359) — прок на восстановление ярости;
- `spell_warr_meat_cleaver_talent` (280392), `spell_warr_brutal_finish_talent` (446085);
- `spell_pal_expurgation` (383344), `spell_pal_eye_for_an_eye` (205191);
- и т.д.

## 5. Ядро: что работает БЕЗ скриптов

Базовая механика способностей (урон, хил, стоимость силы, кд, ауры, описания эффектов)
живёт в ядре и **опирается на DBC** (`Spell.dbc`, aura-эффекты). Это не скрипты, а движок:

```
src/server/game/Spells/
├── Spell.cpp              (395 КБ — сама реализация каста/эффектов)
├── SpellEffects.cpp       (238 КБ — все SPELL_EFFECT_*)
├── SpellInfo.cpp          (233 КБ — чтение/валидация данных спелла)
├── SpellMgr.cpp           (216 КБ — загрузка, правила, каст-логика)
├── SpellScript.h          (90 КБ — API для ваших скриптов: все хуки)
├── TraitMgr.cpp           (таланты/трейты)
└── Auras/
    ├── SpellAuraEffects.cpp   (все SPELL_AURA_* — встроенные ауры)
    ├── SpellAuraDefines.h
    ├── SpellAuras.cpp/.h
    └── ...
```

Т.е. **скрипт нужен только там, где DBC-описания не хватает** (проки, условная логика,
модификация цели/урона, каст дополнительных спеллов и т.п.).

## 6. Шпаргалка по хукам (API в `SpellScript.h`)

### `SpellScript` (срабатывает при касте)

| Хук | Когда | Пример подключения |
|---|---|---|
| `OnCheckCast` | до проверки каста, можно переопределить результат | `OnCheckCast += SpellCheckCastFn(F);` |
| `BeforeCast` / `OnCast` / `AfterCast` | до/в момент/после успешного каста | `AfterCast += SpellCastFn(F);` |
| `OnEffectLaunch` / `OnEffectLaunchTarget` | запуск эффекта (по спеллу/по цели) | `OnEffectLaunch += SpellEffectFn(F, EFFECT_0, SPELL_AURA_DUMMY);` |
| `OnEffectHit` / `OnEffectHitTarget` | эффект попал в цель | аналогично |
| `OnEffectSuccessfulDispel` | успешное снятие | — |
| `BeforeHit` / `OnHit` / `AfterHit` | по попаданию спелла | `OnHit += SpellHitFn(F);` |

### `AuraScript` (срабатывает от аур: проки, тики, кастомные расчёты)

| Хук | Когда | Пример |
|---|---|---|
| `DoCheckProc` / `OnCheckProc` | валидация прока (вернуть false = не прокает) | `DoCheckEffectProc += AuraCheckEffectProcFn(F, EFFECT_0, SPELL_AURA_DUMMY);` |
| `OnEffectProc` / `AfterEffectProc` | сработал прок-эффект | `OnEffectProc += AuraEffectProcFn(F, EFFECT_0, SPELL_AURA_DUMMY);` |
| `OnPeriodicEffect` / `DoPeriodicEffect` | тик ауры | `DoPeriodicEffect += AuraPeriodicFn(F);` |
| `DoEffectCalcAmount` | пересчёт значения эффекта | `DoEffectCalcAmount += AuraEffectCalcAmountFn(F, EFFECT_0, SPELL_AURA_MOD_DAMAGE_DONE);` |

Полезные методы внутри скрипта: `GetCaster()`, `GetTarget()`, `GetHitUnit()`,
`GetSpell()`, `CastSpell()`, `PreventDefaultAction()`, `ValidateSpellInfo({...})`,
`eventInfo.GetProcSpell()`, `eventInfo.GetActionTarget()`, `ApplySpellMod(...)` и пр.

## 7. Рецепт: как добавить свой скрипт на класс/талант

1. **Найти ID.**
   - WCL: в отчёте (JSON/справочник способностей) каждая способность имеет `id`
     (например Execute = 20647); по отчёту виден полный набор ID, которые реально кастует проф.
   - Wowhead: ID прямо в URL — `wowhead.com/spell=20647/execute`; для талантов — страницы
     классных талантов.
   - Финальная проверка: `Spell.dbc` из папки `data/` сервера — единственный источник
     правды, что клиент и сервер видят одно и то же.
2. **Написать скрипт** — в файл своего класса (`src/server/scripts/Spells/spell_*.cpp`,
   алфавитный порядок!) или в `src/server/scripts/Custom/` для своего кастомного контента:
   - добавить ID в `enum` файла;
   - `class spell_warr_my_ability : public SpellScript` (или `AuraScript`);
   - `Validate()` → `ValidateSpellInfo({ ... })`;
   - `Register()` → хуки из таблицы выше.
3. **Зарегистрировать**: в `AddSC_<class>_spell_scripts()` (или `AddCustomScripts()`
   в `Custom/custom_script_loader.cpp`) — `RegisterSpellScript(...)` /
   `RegisterSpellAndAuraScriptPair(spell, aura)` / `new class;`.
4. **Привязать ID** в world-БД — INSERT в `spell_script_names` (update-файл
   `sql/updates/world/master/…world.sql`).
5. **CMake править не нужно** — модули-скрипты собираются автоматическим глобом
   (`cmake/macros/ConfigureScripts.cmake`), но после добавления нового файла
   CMake надо перезапустить (GLOB подхватит файл при конфигурации).
6. Пересобрать, `/reload spell_script_names` (или рестарт сервера).

## 8. Ключевые пути (шпаргалка)

```
src/server/scripts/Spells/            ← СЮДА: скрипты способностей классов
src/server/scripts/Custom/            ← СЮДА: ваши кастомные скрипты
src/server/game/Spells/SpellScript.h  ← API хуков
src/server/game/Spells/Spell*.cpp     ← движок спеллов
src/server/game/Spells/Auras/         ← встроенные ауры
src/server/game/Spells/TraitMgr.cpp   ← таланты (traits)
src/server/game/Handlers/TraitHandler.cpp
src/server/game/Scripting/ScriptMgr.cpp   ← регистрация/запуск скриптов
src/server/game/Globals/ObjectMgr.cpp     ← загрузка spell_script_names
sql/updates/world/master/             ← SQL-апдейты (новые привязки)
doc/HowToScript.txt                   ← базовая документация
```
