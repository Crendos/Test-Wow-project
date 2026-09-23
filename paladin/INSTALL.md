# 🛠️ ИНСТРУКЦИЯ ДЛЯ ПК — паладин-фиксы 12.1.0 (TrinityCore master, Windows)

Что делаем: ставим **паладин-фиксы** в ваш сервер: 1 патч ядра + 10 файлов со скриптами + 10 SQL-файлов.
Порядок строгий: **Шаг 0 → 1 → 2 → 3 → 4**.

> ⚠️ **ВАЖНО ПРО ТЕРМИНАЛ (частая беда):**
> 1. **Ctrl+V в Git Bash НЕ ВСТАВЛЯЕТ** — поэтому «ноль реакции» при копировании из Notepad++.
>    Вставка в Git Bash: **правый клик мышью** (или **Shift+Insert**).
> 2. bash-команды **молчат при успехе** — это норма, а не глюк.
> 3. **Не хотите бороться с консолью вообще** — используйте готовые скрипты из папки
>    `paladin/windows/` (ШАГ 1 и ШАГ 2 запускаются одной короткой командой — см. их разделы).
>    Если правите скрипты в Notepad++ — сохраняйте с концами строк **Unix (LF)**
>    (правый нижний угол Notepad++: CRLF → LF), либо запускайте через
>    `bash <(tr -d '\r' < файл.sh)` — это лечит любой вариант.

Скачайте папку `paladin/` из этого репозитория (зелёная кнопка **Code → Download ZIP**,
распакуйте, например, в `C:\paladin-fixes\paladin`). Дальше я пишу пути так:
- `C:\paladin-fixes\paladin` — папка с фиксами (у вас может быть другой);
- `C:\TrinityCore` — папка с ИСХОДНИКАМИ вашего ядра (где лежит `src`, `CMakeLists.txt`).

---

## Что должно быть установлено (если сервер уже собирали — пропустите)

1. **Git for Windows** — git-scm.com (при установке всё «далее→далее»).
2. **Visual Studio 2022 Community** — visualstudio.microsoft.com. При установке отметьте
   рабочую нагрузку **«Разработка классических приложений на C++»**.
3. **CMake** — идёт вместе с VS (проверьте галочку «CMake tools»), либо cmake.org.
4. **Boost** (1.78+) — либо prebuilt-архив (boost.org / SourceForge), либо `vcpkg install boost`.
5. **MySQL Server 8.x** + **HeidiSQL** (GUI для базы, heidisql.com).
6. **OpenSSL** (сборка TC его требует).

---

## ШАГ 0. Патч ядра — блок заклинаний (мастерство Прота)

Что это: без него «Божественный оплот» не блокирует заклинания.

### Вариант А — через командную строку (30 секунд)
Правый клик по `C:\TrinityCore` → **Git Bash Here**, в открывшемся окне:

```bash
git apply --check "C:/paladin-fixes/paladin/core_patch/0001-core-spell-block-chance.patch"
```
Если ничего не напечатало — всё чисто (git apply молчит при успехе). Применяем:
```bash
git apply "C:/paladin-fixes/paladin/core_patch/0001-core-spell-block-chance.patch" && echo "=== ПАТЧ УСТАНОВЛЕН ==="
```

### Вариант Б — вручную в Notepad++ (если git'у не доверяете)

**Файл 1:** `C:\TrinityCore\src\server\game\Entities\Unit\Unit.cpp`
1. Ctrl+F → найдите строку: `if (CanApplyResilience())`
   (нужно вхождение внутри функции `Unit::CalculateSpellDamageTaken` — прямо над ним строка `damage = Unit::SpellCriticalDamageBonus(this, spellInfo, damage, victim);` и пара закрывающих скобок).
2. **ПЕРЕД** строкой `if (CanApplyResilience())` вставьте этот блок:

```cpp
                // Spell blocking (SPELL_AURA_MOD_SPELL_BLOCK_CHANCE): masteries/talents
                // (e.g. Protection paladin "Mastery: Divine Bulwark") can block spells.
                // The DBC MValue coefficient (2% block chance per mastery point) is not
                // loaded by TC, so the chance is recomputed here from mastery points.
                if (victim->HasAuraType(SPELL_AURA_MOD_SPELL_BLOCK_CHANCE))
                {
                    float spellBlockChance = 0.f;
                    if (Player* playerVictim = victim->ToPlayer())
                        spellBlockChance = (playerVictim->GetTotalAuraModifier(SPELL_AURA_MASTERY)
                            + playerVictim->GetRatingBonusValue(CR_MASTERY)) * 2.0f;

                    if (spellBlockChance > 0.f && roll_chance(spellBlockChance))
                    {
                        uint32 value = victim->GetBlockPercent(GetLevel());
                        if (victim->IsBlockCritical())
                        {
                            value *= 2; // double blocked amount if block is critical
                            value = uint32(value * GetTotalAuraMultiplier(SPELL_AURA_MOD_CRITICAL_BLOCK_AMOUNT));
                        }

                        damageInfo->blocked = CalculatePct(damage, value);
                        if (damage <= int32(damageInfo->blocked))
                        {
                            damageInfo->blocked = uint32(damage);
                            damageInfo->fullBlock = true;
                        }
                        damage -= damageInfo->blocked;
                    }
                }

```

**Файл 2:** `C:\TrinityCore\src\server\game\Spells\Auras\SpellAuraEffects.cpp`
1. Ctrl+F → найдите: `//529 SPELL_AURA_MOD_SPELL_BLOCK_CHANCE`
2. Строку
```cpp
    &AuraEffect::HandleNULL,                                      //529 SPELL_AURA_MOD_SPELL_BLOCK_CHANCE
```
замените на
```cpp
    &AuraEffect::HandleNoImmediateEffect,                         //529 SPELL_AURA_MOD_SPELL_BLOCK_CHANCE implemented in Unit::CalculateSpellDamageTaken
```

---

## ШАГ 1. Скрипты паладина (10 файлов)

### 1.1. Вставить код в spell_paladin.cpp

**Вариант «bat-скриптом» (БЕЗ Git Bash и БЕЗ указания путей).**
В папке `paladin\windows\` лежит **`step1_scripts.bat`** — просто **дважды кликните по нему**.
Он сам:
- найдёт папку фиксов (рядом с собой) и корень ядра (типовые места, включая
  `Desktop\new\test\TrinityCore`); если не найдёт — спросит путь (в окно вставляется правым кликом);
- допишет все 10 файлов, проверит счётчиком (партий: 10), при повторном запуске не задвоит;
- напомнит про правки лоадера (1.2а/1.2б) и проверит их после.

Из консоли (x64 Native Tools / cmd) то же самое: `"C:\...\windows\step1_scripts.bat"` (аргументы не нужны).

**Вариант «sh-скриптом» (через Git Bash, консоль почти не нужна).**
Наберите руками две короткие строки (Ctrl+V тут не работает — набор вручную надёжен):

```
cd "C:/Users/USER/Desktop/new/test/TrinityCore"
```
затем
```
bash <(tr -d '\r' < "C:/Users/USER/Desktop/new/paladin/windows/step1_scripts.sh")
```
Скрипт сам: проверит, что вы в корне ядра, допишет все 10 файлов, проверит счётчиком
(10 = успех), при повторном запуске не задвоит, в конце напечатает
`>>> ШАГ 1 ВЫПОЛНЕН УСПЕШНО`. Если папка фиксов в другом месте — добавьте её вторым аргументом:
`bash <(tr -d '\r' < ".../step1_scripts.sh") "C:/мой/путь/paladin"`.

**Автоматически (вставкой блока, если правый клик-вставка работает).** Правый клик по `C:\TrinityCore` → **Git Bash Here**. Скопируйте блок ЦЕЛИКОМ и вставьте:

```bash
echo "=== [1/3] Дописываю 10 файлов в spell_paladin.cpp... ==="
for i in "" _2 _3 _4 _5 _6 _7 _8 _9 _10; do
  sed -n '/=== CUT HERE ===/,$p' "C:/paladin-fixes/paladin/spell_paladin_class_fixes${i}.cpp" >> src/server/scripts/Spells/spell_paladin.cpp
  echo "    добавлен: spell_paladin_class_fixes${i}.cpp"
done
echo "=== [2/3] Проверяю: сколько партий внутри файла... ==="
N=$(grep -c "=== CUT HERE ===" src/server/scripts/Spells/spell_paladin.cpp); echo ">>> в файле партий: $N (нужно 10)"
[ "$N" = "10" ] && echo ">>> ГОТОВО: все 10 партий внутри" || echo ">>> ВНИМАНИЕ: партий не 10 — см. раздел неполадок"
echo "=== [3/3] Последние строки файла: ==="
tail -3 src/server/scripts/Spells/spell_paladin.cpp
```
Ожидаемый вывод: десять строк `добавлен: …`, затем `>>> в файле партий: 10 (нужно 10)` и `>>> ГОТОВО`, а в хвосте — строка `AddSC_paladin_spell_scripts_ex10()`.
Если повторяете команду второй раз — сначала откатите: `git checkout -- src/server/scripts/Spells/spell_paladin.cpp` (иначе партии лягут вторым слоем и счётчик покажет 20).

**✅ Проверка шага 1.1:**
```bash
grep -c "=== CUT HERE ===" src/server/scripts/Spells/spell_paladin.cpp
```
Должно напечатать **10** (все партии внутри). `0` — вставки не было; 1–9 — повторите цикл (частичный повтор безопасен: помеченные куски допишутся вторым слоем, но чтобы было чисто — лучше восстановить spell_paladin.cpp из git: `git checkout -- src/server/scripts/Spells/spell_paladin.cpp` и выполнить цикл заново).

**Вручную (если хочется руками):**
1. Откройте `C:\TrinityCore\src\server\scripts\Spells\spell_paladin.cpp` в Notepad++.
2. Нажмите **Ctrl+End** (в самый конец файла), поставьте курсор на новую строку.
3. Для КАЖДОГО из 10 файлов (`spell_paladin_class_fixes.cpp`, `_2.cpp`, `_3.cpp`, `_4.cpp`,
   `_5.cpp`, `_6.cpp`, `_7.cpp`, `_8.cpp`, `_9.cpp`, `_10.cpp`) из `C:\paladin-fixes\paladin`:
   откройте его, найдите строку `=== CUT HERE ===`, выделите **от этой строки до конца файла**,
   скопируйте и вставьте в конец spell_paladin.cpp. **Порядок 1→10 важен.**

### 1.2. Зарегистрировать скрипты в лоадере

Откройте `C:\TrinityCore\src\server\scripts\Spells\spell_script_loader.cpp`:

**а)** Найдите строку `void AddSC_paladin_spell_scripts();` — **ПОСЛЕ неё** добавьте:
```cpp
void AddSC_paladin_spell_scripts_ex2();
void AddSC_paladin_spell_scripts_ex3();
void AddSC_paladin_spell_scripts_ex4();
void AddSC_paladin_spell_scripts_ex5();
void AddSC_paladin_spell_scripts_ex6();
void AddSC_paladin_spell_scripts_ex7();
void AddSC_paladin_spell_scripts_ex8();
void AddSC_paladin_spell_scripts_ex9();
void AddSC_paladin_spell_scripts_ex10();
```

**б)** Найдите строку `AddSC_paladin_spell_scripts();` (внутри функции `AddSpellsScripts()`) —
**ПОСЛЕ неё** добавьте:
```cpp
    AddSC_paladin_spell_scripts_ex2();
    AddSC_paladin_spell_scripts_ex3();
    AddSC_paladin_spell_scripts_ex4();
    AddSC_paladin_spell_scripts_ex5();
    AddSC_paladin_spell_scripts_ex6();
    AddSC_paladin_spell_scripts_ex7();
    AddSC_paladin_spell_scripts_ex8();
    AddSC_paladin_spell_scripts_ex9();
    AddSC_paladin_spell_scripts_ex10();
```

**✅ Проверка шага 1.2 (говорящая):**
```bash
A=$(grep -c "void AddSC_paladin_spell_scripts_ex" src/server/scripts/Spells/spell_script_loader.cpp)
B=$(grep -c "    AddSC_paladin_spell_scripts_ex" src/server/scripts/Spells/spell_script_loader.cpp)
echo ">>> объявлений: $A (нужно 9)"; echo ">>> вызовов: $B (нужно 9)"
[ "$A" = "9" ] && [ "$B" = "9" ] && echo ">>> ЛОАДЕР ГОТОВ" || echo ">>> допишите блоки 1.2а/1.2б вручную (Notepad++)"
```

### ✅ Всё вместе: проверка шагов 0–1 одним блоком
```bash
echo "--- Шаг 0: патч ядра (ждём 3) ---"; grep -c "spellBlockChance" src/server/game/Entities/Unit/Unit.cpp
echo "--- Шаг 1: партии (ждём 10) ---"; grep -c "=== CUT HERE ===" src/server/scripts/Spells/spell_paladin.cpp
echo "--- Шаг 1: объявления (ждём 9) ---"; grep -c "void AddSC_paladin_spell_scripts_ex" src/server/scripts/Spells/spell_script_loader.cpp
echo "--- Шаг 1: вызовы (ждём 9) ---"; grep -c "    AddSC_paladin_spell_scripts_ex" src/server/scripts/Spells/spell_script_loader.cpp
echo "--- Проверки завершены ---"
```
Все четыре цифры совпали — переходите к Шагу 2.

---

## ШАГ 2. База данных (HeidiSQL, по кликам)

1. Остановите **worldserver** (если запущен).
2. Откройте HeidiSQL → **New** → Host `127.0.0.1`, User `root`, ваш пароль → **Open**.
3. Слева раскройте сервер и **кликните один раз** на вашу **world**-базу
   (имя смотрите в `worldserver.conf`, строка `WorldDatabaseInfo = "127.0.0.1;3306;root;пароль;имябазы"`).
4. Меню **File → Run SQL file…** → выберите и выполните **по очереди**:
   - `C:\paladin-fixes\paladin\paladin_class_fixes.sql`
   - `C:\paladin-fixes\paladin\paladin_class_fixes_2.sql`
   - `C:\paladin-fixes\paladin\paladin_class_fixes_3.sql`
   - `C:\paladin-fixes\paladin\paladin_class_fixes_4.sql`
   - `C:\paladin-fixes\paladin\paladin_class_fixes_5.sql`
   - `C:\paladin-fixes\paladin\paladin_class_fixes_6.sql`
   - `C:\paladin-fixes\paladin\paladin_class_fixes_7.sql`
   - `C:\paladin-fixes\paladin\paladin_class_fixes_8.sql`  ← спираль Благословенного молота
   - `C:\paladin-fixes\paladin\paladin_class_fixes_9.sql`
   - `C:\paladin-fixes\paladin\paladin_class_fixes_10.sql`
   (каждый — отдельный запуск Run SQL file; повторный прогон безопасен).

**Через консоль (cmd/консоль VS, если `mysql.exe` в PATH) — с подтверждением каждого файла:**
```bash
D=0; F=0
for i in "" _2 _3 _4 _5 _6 _7 _8 _9 _10; do
  echo "--- импортирую paladin_class_fixes${i}.sql... ---"
  if mysql -u root -pВАШ_ПАРОЛЬ ИМЯ_БАЗЫ < "C:/paladin-fixes/paladin/paladin_class_fixes${i}.sql"; then echo ">>> OK: ${i:-1}"; D=$((D+1)); else echo ">>> ОШИБКА на ${i:-1}"; F=$((F+1)); fi
done
echo "=== ИТОГ: успешно $D из 10, с ошибками $F ==="
```
Должно напечатать `ИТОГ: успешно 10 из 10, с ошибками 0`.

**✅ Проверка импорта.** В HeidiSQL откройте вкладку **Query**, выполните (вывод прямо говорит, что применилось):
```sql
SELECT CONCAT('1) Бинды скриптов: ', COUNT(*), ' (ждём больше 50)') AS ПРОВЕРКА FROM spell_script_names WHERE ScriptName LIKE 'spell_pal%'
UNION ALL
SELECT CONCAT('2) Спираль молота AT 6006: ', COUNT(*), ' (ждём 1)') FROM areatrigger_create_properties WHERE Id=6006 AND IsCustom=0
UNION ALL
SELECT CONCAT('3) Точки спирали: ', COUNT(*), ' (49 = наша спираль; 0 = строка из TDB)') FROM areatrigger_create_properties_spline_point WHERE AreaTriggerCreatePropertiesId=6006 AND IsCustom=0
UNION ALL
SELECT CONCAT('4) Проц-строки (Спасенный/Наказание): ', COUNT(*), ' (ждём 2)') FROM spell_proc WHERE SpellId IN (157047, 431474);
```
Если хоть одна строка не совпала с ожиданием — соответствующий SQL-файл не импортировался, прогоните его заново (повтор безопасен).

---

## ШАГ 3. Сборка ядра

### Если сервер УЖЕ собирался:
1. Откройте `C:\TrinityCore\build\TrinityCore.sln` в Visual Studio.
2. Сверху выберите конфигурацию **Release**, платформу **x64**.
3. В окне Solution Explorer правый клик по **ALL_BUILD** → **Build**.
4. Ждите. В конце должно быть `========== Build: succeeded, 0 failed ==========` (любое число failed — кидайте мне текст ошибки).

**Общая проверка всего (0–1 + сборка) одним скриптом:**

— **двойной клик по `paladin\windows\check_all.bat`** (ядро найдёт сам; если нет — спросит путь):
Напечатает `>>> OK:` по каждому пункту с фактическими числами и в конце
`=== ВСЁ СХОДИТСЯ ===` либо список расхождений (этот вывод удобно прислать мне).

**✅ Проверка шага 3** (что скрипты реально попали в exe; Git Bash, из папки ядра):
```bash
grep -a -c "spell_pal_glory_of_the_vanguard_ex" build/bin/Release/worldserver.exe
```
Напечатает **1** или больше — скрипты внутри сборки. Напечатает `0` — вставки из Шага 1 не попали в сборку (перепроверьте шаги 1.1–1.2 и пересоберите).
В обычной командной строке (cmd) то же самое:
```cmd
findstr /m /c:"spell_pal_glory_of_the_vanguard_ex" "C:\TrinityCore\build\bin\Release\worldserver.exe"
```
(напечатает путь к exe, если строка найдена; молчит — если нет).

### Через командную строку — x64 Native Tools Command Prompt for VS 2022

Кому удобнее консоль (быстрее GUI-сборки, печатает прогресс в процентах, легко повторять):

1. **Пуск → Visual Studio 2022 → x64 Native Tools Command Prompt for VS 2022**
   (именно этот ярлык — он сам прописывает пути к компилятору; обычный cmd НЕ подойдёт).
2. Проверка, что вы в том самом окне: наберите `msbuild -version` — напечатает номер, а не «не является командой».

**Если сервер УЖЕ собирался** (есть `TrinityCore.sln`):
```bat
cd /d C:\TrinityCore\build
msbuild TrinityCore.sln /p:Configuration=Release /p:Platform=x64 /m
```
`/m` — параллельная сборка (все ядра CPU). Соберёт всё, включая ваши правки Шагов 0–1.
Готовые `worldserver.exe`/`authserver.exe` появятся (обновятся) в `C:\TrinityCore\build\bin\Release\`.

**Собрать только серверы (быстрее, чем весь sln):**
```bat
cd /d C:\TrinityCore\build
msbuild worldserver.vcxproj /p:Configuration=Release /m
msbuild authserver.vcxproj /p:Configuration=Release /m
```

**Если с нуля** (той же консолью, cmake подтянется из VS; при ругани на Boost добавьте `-DBOOST_ROOT=C:/boost`):
```bat
cd /d C:\TrinityCore
cmake -S . -B build -A x64
cmake --build build --config Release -j 8
```

**✅ Проверка шага 3 той же консолью** (0 = не собралось со скриптами, путь = собралось):
```bat
findstr /m /c:"spell_pal_glory_of_the_vanguard_ex" "C:\TrinityCore\build\bin\Release\worldserver.exe"
```

### Если с нуля (кратко, через GUI):
1. **CMake (cmake-gui)**: «Where is the source code» = `C:/TrinityCore`; «Where to build» = `C:/TrinityCore/build` → **Configure** → Visual Studio 17 2022, x64.
   Если ругнётся на Boost — добавьте запись `BOOST_ROOT` = путь к распакованному Boost и снова **Configure** (до исчезновения красных строк).
2. **Generate** → **Open Project** → в VS: Release/x64 → ALL_BUILD → Build (займёт 20–60 мин).
3. Готовые серверы появятся в `C:\TrinityCore\build\bin\Release\` (`worldserver.exe`, `authserver.exe`).

> Ваши правки (шаги 0–1) подхватятся этой же пересборкой — отдельно ничего копировать в исходники не нужно.

---

## ШАГ 4. Запуск и проверка в игре

1. Скопируйте свежие `worldserver.exe`/`authserver.exe` (и `bin\Release\*.dll` рядом, если появятся) туда, где живёт ваш сервер.
2. Запустите `authserver.exe`, затем `worldserver.exe`. Дождитесь `World initialized`.
   В логе старта НЕ должно быть ошибок вида `Script 'spell_pal_…' not found`.
3. Зайдите паладином (можно .learn все таланты) и проверьте хотя бы это:

| Действие | Что должно произойти |
|---|---|
| Прот: каст Благословенного молота в 2–3 мобов | видно **вращающийся по спирали молот**; урон каждому врагу по мере прохода; в логах «Blocked» по магии (с мастерством) |
| Прот: Правосудие до ЩП | с шансом ~23% появляется бафф **Авангард**; следующий Щит мстителя бьёт болтом Света (через ~0.3 с) |
| Хил: Слово света при активном Благ. горна | цель дополнительно получает **Святое слово** (лечение) |
| Рет: Клинок справедливости | рядом с обычным уроном летит **Свет в душе** |
| Любая спека: щиты талантов | Спасенный Светом/Серафимский барьер вешают щит (визуал может быть «прот-овским» — см. «Упрощения») |

---

## Если что-то не работает — типовые причины

| Симптом | Причина | Лечение |
|---|---|---|
| `git apply` НЕ напечатал ничего | **Это успех!** git apply молчит при успехе | проверка: `git diff --stat` — должны появиться Unit.cpp (+30) и SpellAuraEffects.cpp (+2/-1) |
| `^[[200~` прилип к команде | мусор терминала при вставке (bracketed paste) | не страшно: наберите команду руками или используйте прямые слэши `/` в пути |
| перед командой вводили `bash` | не нужно: Git Bash уже открыт (MINGW64 в приглашении) | вводите сразу `git apply ...` |
| повторный `git apply`: `already exists` / `patch failed` | патч уже стоит с прошлого раза | ничего не делайте — переходите к Шагу 1 |
|---|---|---|
| `git apply` ругается на строки | исходники отличаются от master, к которому писался патч | применяйте Вариант Б (вручную) — там указано «найди/замени» |
| Ошибка компиляции: `SPELL_EX…: identifier not found` | вставили файлы не по порядку или не все 10 | повторите Шаг 1.1 (порядок 1→10) |
| Ошибка: `AddSC_paladin_spell_scripts_ex9 undefined` | не добавили блок 1.2-б | допишите вызовы в `AddSpellsScripts()` |
| В игре скрипты молчат, урон голый | не импортирован SQL (Шаг 2) или worldserver не перезапускался | импортируйте все 10 SQL и перезапустите worldserver |
| Молот без спирали | не импортирован `_8.sql` | проверьте запрос `areatrigger_create_properties` из Шага 2; работает и без него — будет мгновенный AoE (fallback) |

---

## Известные упрощения v1 (не баги — сознательные допущения)

- Благословенный молот: спираль готова (AT 6006 + AI-скрипт); до импорта `_8.sql` — мгновенный AoE;
- Страж — задержка распада упрощена (+1с длительности за трату СС);
- Маяк веры — 2-й маяк с полным переносом (для 70% — примечание в шапке `spell_paladin_class_fixes_5.cpp`);
- Наставляемая молитва — Слово света полной силы (в игре 60%);
- Серафимский барьер / Переполняющий свет — щит через носитель 209388 (визуал Прота, значение точное);
- Маяк Спасителя — перенос 10/20%, пере-выбор цели каждые 2с;
- Слава авангарда — болт без полёта «по линии» (цель + DBC-эффекты 1269175), задержка 300 мс есть.

Полный статус по каждому таланту — `PALADIN_AUDIT.md`; измеренные значения по WCL — `logs_analysis/README.md`.
