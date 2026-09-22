# ПОЛНАЯ ИНСТРУКЦИЯ: фикс паладина 12.1.0 в TrinityCore (полная сборка ядра)

Состав поставки:
```
paladin/
├── spell_paladin_class_fixes.cpp     — скрипты, партия 1 (Ретри)
├── spell_paladin_class_fixes_2.cpp   — скрипты, партия 2 (Ретри-остаток + Защита)
├── spell_paladin_class_fixes_3.cpp   — скрипты, партия 3 (Защита + Молот гнева в АН)
├── spell_paladin_class_fixes_4.cpp   — скрипты, партия 4a (Свет, ядро хила)
├── spell_paladin_class_fixes_5.cpp   — скрипты, партия 4b (Свет, маяки)
├── spell_paladin_class_fixes_6.cpp   — скрипты, партия 5 (классовое дерево)
├── spell_paladin_class_fixes_7.cpp   — скрипты, партия 6 (геройские деревья)
├── spell_paladin_class_fixes_8.cpp   — скрипты, партия 7 (Слава авангарда, Маяк Спасителя,
│                                        Серафимский барьер, Переполняющий свет)
├── paladin_class_fixes.sql           — SQL партии 1
├── paladin_class_fixes_2.sql         — SQL партии 2
├── paladin_class_fixes_3.sql         — SQL партии 3
├── paladin_class_fixes_4.sql         — SQL партий 4a+4b
├── paladin_class_fixes_5.sql         — SQL партии 5+6 (геройские)
├── paladin_class_fixes_7.sql         — SQL партии 7 (по ID пользователя)
└── core_patch/
    └── 0001-core-spell-block-chance.patch  — ПАТЧ ЯДРА (блок заклинаний, мастерство Прота)
```

Порядок: **Шаг 0 (патч ядра) → Шаг 1 (скрипты) → Шаг 2 (БД) → Шаг 3 (сборка) → Шаг 4 (проверка)**.

---

## ШАГ 0. Патч ядра — блок заклинаний (мастерство Защиты «Божественный оплот»)

Зачем: в TC master аура `SPELL_AURA_MOD_SPELL_BLOCK_CHANCE` (529) не реализована — шанс блока заклинаний никогда не роллится. Патч добавляет ролл в `Unit::CalculateSpellDamageTaken` (шанс = очки мастерства × 2%, как в DBC MValue; при блок-крите ×2; учитывает крит. блок). Проверено: файлы компилируются, proc-маска сама ставит PROC_HIT_BLOCK.

Применение (из корня ВАШЕГО исходника TC):

```bash
git apply --check paladin/core_patch/0001-core-spell-block-chance.patch   # проверка
git apply paladin/core_patch/0001-core-spell-block-chance.patch           # применить
```

Если `--check` ругается (ваш исходник отличается от master) — примените вручную:

**Файл 1: `src/server/game/Entities/Unit/Unit.cpp`**
Найдите функцию `void Unit::CalculateSpellDamageTaken(...)`, внутри неё ветку:
```cpp
            // Magical Attacks
            case SPELL_DAMAGE_CLASS_NONE:
            case SPELL_DAMAGE_CLASS_MAGIC:
            {
                // If crit add critical bonus
                if (crit)
                {
                    ...
                }

                if (CanApplyResilience())
```
Между блоком `if (crit) { ... }` и `if (CanApplyResilience())` вставьте 30 строк из патча (блок `if (victim->HasAuraType(SPELL_AURA_MOD_SPELL_BLOCK_CHANCE))` — копия из файла патча, строки с `+`).

**Файл 2: `src/server/game/Spells/Auras/SpellAuraEffects.cpp`**
Найдите строку (около 601):
```cpp
    &AuraEffect::HandleNULL,                                      //529 SPELL_AURA_MOD_SPELL_BLOCK_CHANCE
```
замените на:
```cpp
    &AuraEffect::HandleNoImmediateEffect,                         //529 SPELL_AURA_MOD_SPELL_BLOCK_CHANCE implemented in Unit::CalculateSpellDamageTaken
```

> Патч опционален: без него всё остальное работает, только Прот не будет блокировать заклинания.

---

## ШАГ 1. Скрипты (8 файлов)

### 1.1. Вставка кода
Откройте `src/server/scripts/Spells/spell_paladin.cpp`. Прокрутите в САМЫЙ КОНЕЦ файла
(после последней закрывающей скобки и после `AddSC_paladin_spell_scripts()` — там обычно `// Add all scripts in the proper order` и функция регистрации).

В конец файла вставьте содержимое всех 7 файлов, **строго в этом порядке**:
1. `spell_paladin_class_fixes.cpp` — всё, что ПОСЛЕ строки `=== CUT HERE ===`
2. `spell_paladin_class_fixes_2.cpp` — так же
3. `spell_paladin_class_fixes_3.cpp`
4. `spell_paladin_class_fixes_4.cpp`
5. `spell_paladin_class_fixes_5.cpp`
6. `spell_paladin_class_fixes_6.cpp`
7. `spell_paladin_class_fixes_7.cpp`
8. `spell_paladin_class_fixes_8.cpp`

Порядок важен: партии 4a–6 используют хелперы (`IsPaladinJudgment`, `GetHolyPowerCost`), объявленные в партии 1.

Быстро из командной строки (из корня репозитория с фиксами):
```bash
for i in "" _2 _3 _4 _5 _6 _7 _8; do
  sed -n '/=== CUT HERE ===/,$p' paladin/spell_paladin_class_fixes${i}.cpp >> src/server/scripts/Spells/spell_paladin.cpp
done
```

### 1.2. Регистрация в лоадере
Откройте `src/server/scripts/Spells/spell_script_loader.cpp`:

**а)** Вверху, к списку объявлений (там где `void AddSC_paladin_spell_scripts();`),
добавьте ПОСЛЕ него:
```cpp
void AddSC_paladin_spell_scripts_ex2();
void AddSC_paladin_spell_scripts_ex3();
void AddSC_paladin_spell_scripts_ex4();
void AddSC_paladin_spell_scripts_ex5();
void AddSC_paladin_spell_scripts_ex6();
void AddSC_paladin_spell_scripts_ex7();
void AddSC_paladin_spell_scripts_ex8();
```
(части 1 не нужно — её регистрация уже вызывается; х4a объявляется внутри ex4? — НЕТ: ex4 = партия 4a, ex5 = 4b; обе объявляются здесь.)

**б)** Внутри `AddSpellsScripts()` найдите `AddSC_paladin_spell_scripts();` и добавьте ПОСЛЕ него:
```cpp
    AddSC_paladin_spell_scripts_ex2();
    AddSC_paladin_spell_scripts_ex3();
    AddSC_paladin_spell_scripts_ex4();
    AddSC_paladin_spell_scripts_ex5();
    AddSC_paladin_spell_scripts_ex6();
    AddSC_paladin_spell_scripts_ex7();
    AddSC_paladin_spell_scripts_ex8();
```

---

## ШАГ 2. База данных

Выполнить на **world**-базу (та, что указана в `worldserver.conf` → `WorldDatabaseInfo`).
Все запросы `REPLACE INTO` — повторный прогон безопасен.

```bash
mysql -u root -p <имя_world_базы> < paladin/paladin_class_fixes.sql
mysql -u root -p <имя_world_базы> < paladin/paladin_class_fixes_2.sql
mysql -u root -p <имя_world_базы> < paladin/paladin_class_fixes_3.sql
mysql -u root -p <имя_world_базы> < paladin/paladin_class_fixes_4.sql
mysql -u root -p <имя_world_базы> < paladin/paladin_class_fixes_5.sql
mysql -u root -p <имя_world_базы> < paladin/paladin_class_fixes_6.sql
mysql -u root -p <имя_world_базы> < paladin/paladin_class_fixes_7.sql
mysql -u root -p <имя_world_базы> < paladin/paladin_class_fixes_8.sql
```
(например: `mysql -u root -p acore_world < paladin/paladin_class_fixes.sql`)

Что делают:
- `spell_script_names` — привязывают скрипты к ID заклинаний;
- `spell_proc` — включают проц-ауры, которые БЕЗ строки в этой таблице не работают вовсе
  (включая P0-фикс «Крещендо ударов» 406833 и блок-зависимые процы);
- `_6.sql` — проц-таблица для партии 6 (классовое дерево);
- `_8.sql` — спираль Благословенного молота: строка `areatrigger_create_properties` (Id 6006)
  + 49 точек сплайна + привязка AI-скрипта `at_pal_blessed_hammer`.

Проверка спирали молота после импорта:
```sql
SELECT Id, Shape, Speed, SpeedIsTime, ScriptName FROM areatrigger_create_properties WHERE Id=6006 AND IsCustom=0;
SELECT COUNT(*) FROM areatrigger_create_properties_spline_point WHERE AreaTriggerCreatePropertiesId=6006 AND IsCustom=0;
```
(вторая строка вернёт 49, если спираль создали мы; 0 — если TDB содержит свою розничную строку, это тоже нормально)

---

## ШАГ 3. Полная сборка ядра

```bash
cd <исходник TC>
mkdir -p build && cd build            # или используйте существующую папку сборки
cmake .. -DCMAKE_INSTALL_PREFIX=<куда ставить> -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```
(если собирали раньше — просто `cmake --build . -j$(nproc)`, пересоберутся только изменённые TU: Unit.cpp, SpellAuraEffects.cpp, spell_paladin.cpp, spell_script_loader.cpp)

Дальше стандартно: `install`, обновить карты/DBC не требуется, перезапустить worldserver.

---

## ШАГ 4. Проверка в игре (чек-лист)

| Тест | Ожидание |
|---|---|
| Ретри: автоатака | шанс → «Искусство войны» (бафф, КД Клинка сброшен) |
| Ретри: трата СС | «Праведная причина» иногда сбрасывает Клинок |
| Ретри: Удар крестоносца | шанс → 326733 «Сила небес» (Буря бесплатна) |
| Ретри: Правосудие | иногда «Правосудие Верховного лорда» (багровый взрыв Света) |
| Прот: 3 траты СоП в Щит | «Сияние»: след. Слово света бесплатно |
| Прот: Щит мстителя | стаки Оплота праведной ярости / Силы в невзгодах; −1с КД Хранителя за цель |
| Прот (с патчем): магический урон по вам | блок заклинаний с мастерством (в логах «Blocked») |
| Свет: любое лечение | больше у цели рядом (мастерство Светоносца), до +1.5%/очко |
| Свет: крит Правосудия | «Пробуждение» — след. Правосудие автокрит |
| Гневилище | 5 целей основной способностью |
| Влоге worldserver при старте | нет ошибок загрузки скриптов |

---

## Известные упрощения v1 (см. PALADIN_AUDIT.md)
- ~~Благословенный молот — AoE-урон без вращающейся спирали~~ → ✅ сделано (партия 8: AT-спираль 6006 + AI-скрипт, SQL `_8.sql`);
- Страж — задержка распада упрощена (+1с длительности за трату СС);
- Маяк веры — 2-й маяк с полным переносом (для 70% — примечание в шапке `_5.cpp`);
- Наставляемая молитва — Слово света полной силы (в игре 60%);
- Серафимский барьер/Переполняющий свет — щит через носитель 209388 (визуал Прота, значение точное).
- Маяк Спасителя — перенос 10/20%, пере-выбор цели каждые 2с (условия переноса упрощены).
- Слава авангарда — болт без задержки 300мс и без «по линии» (цель + ДBC-эффекты 1269175).
