# Установка фиксов паладина (12.1.0) в TrinityCore master

Состав: `spell_paladin_class_fixes.cpp` (партия 1) … `_6.cpp` (партия 5),
SQL: `paladin_class_fixes.sql`, `_2.sql`, `_3.sql`, `_4.sql`, `_5.sql`.

## Шаг 1. Код

В конец `src/server/scripts/Spells/spell_paladin.cpp` (перед `AddSC_paladin_spell_scripts()`
или сразу после неё — до конца файла) вставить содержимое КАЖДОГО из 6 файлов
НУЖНО ПО ПОРЯДКУ (1 → 6), беря всё ПОСЛЕ строки `=== CUT HERE ===`.

В `src/server/scripts/Spells/spell_script_loader.cpp`:

```cpp
// к объявлениям сверху:
void AddSC_paladin_spell_scripts_ex2();
void AddSC_paladin_spell_scripts_ex3();
void AddSC_paladin_spell_scripts_ex4();
void AddSC_paladin_spell_scripts_ex5();
void AddSC_paladin_spell_scripts_ex6();

// внутрь AddSpellsScripts(), после AddSC_paladin_spell_scripts():
AddSC_paladin_spell_scripts_ex2();
AddSC_paladin_spell_scripts_ex3();
AddSC_paladin_spell_scripts_ex4();
AddSC_paladin_spell_scripts_ex5();
AddSC_paladin_spell_scripts_ex6();
```

## Шаг 2. База

Выполнить на world-базу (TDB поверх, порядок любой):

```sql
SOURCE paladin/paladin_class_fixes.sql;
SOURCE paladin/paladin_class_fixes_2.sql;
SOURCE paladin/paladin_class_fixes_3.sql;
SOURCE paladin/paladin_class_fixes_4.sql;
SOURCE paladin/paladin_class_fixes_5.sql;
```

Все запросы — `REPLACE INTO`, повторный прогон безопасен.

## Шаг 3. Сборка и проверка

Пересобрать `scripts` (static) и перезапустить worldserver.
В логе при загрузке скриптов новых ошибок быть не должно;
`spell_script_names`/`spell_proc` подхватятся при старте.

## Известные упрощения v1 (см. PALADIN_AUDIT.md, очередь-2)

* Благословенный молот — AoE-урон без вращающейся спирали (AT-визуал).
* Страж — задержка распада упрощена (+1с длительности за трату HP).
* Маяк веры — второй маяк с полным переносом; для 70% нужно дополнить
  TC-скрипт `spell_pal_light_s_beacon` (готовый код в шапке `_5.cpp`).
* Наставляемая молитва — Слово света полной силы (в игре — 60%).
* Молот гнева в АН — оверрайды 1241410/1277026 (в данных есть, «Друг»-вариант 1241413 для Ретри вешается только при наличии соответствующего ранга).
