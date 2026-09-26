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
Откройте **x64 Native Tools Command Prompt for VS 2022** (подходит и обычный cmd — Git Bash не нужен).
Три строки (путь к ядру подставьте свой; если в пути есть пробелы — кавычки обязательны):

```cmd
cd /d C:\TrinityCore
git apply --check "C:\paladin-fixes\paladin\core_patch\0001-core-spell-block-chance.patch"
git apply "C:\paladin-fixes\paladin\core_patch\0001-core-spell-block-chance.patch" && echo === PATCH OK ===
```
Первая команда молчит при успехе (тишина = ОК). Вторая должна напечатать `=== PATCH OK ===`.
Проверка после установки:
```cmd
findstr /n "spellBlockChance" src\server\game\Entities\Unit\Unit.cpp
```
Должна напечататься строка с номером (~1276). Если `git apply` отвечает `already exists` — патч уже стоит, ничего не делайте. После шага 0 ядро нужно пересобрать (Шаг 3).

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

> **Если вы уже ставили старую версию партий** (получали ошибки C2440 при сборке) —
> просто запустите `step1_scripts.bat` ещё раз: он сам увидит старый код, откатит
> `spell_paladin.cpp` через git и вставит новую MSVC-совместимую версию. В конце
> должно быть: `маркеров стало: 10` и `MakeSpellArgs в файле: 18`.

**Автоматически (вставкой блока, если правый клик-вставка работает).** Правый клик по `C:\TrinityCore` → **Git Bash Here**. Скопируйте блок ЦЕЛИКОМ и вставьте (если папка фиксов не `C:\paladin-fixes\paladin` — поправьте путь во 2-й строке):

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
SELECT CONCAT('4) Проц-строки (Спасенный/Наказание): ', COUNT(*), ' (ждём 2)') FROM spell_proc WHERE SpellId IN (157047, 431474)
UNION ALL
SELECT CONCAT('5) Шаблон AT 6006: ', COUNT(*), ' (ждём 1; 0 = КРАШ при касте молота!)') FROM areatrigger_template WHERE Id=6006 AND IsCustom=0;
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

**Проверка исходника ПЕРЕД сборкой** (cmd, одна строка — если напечатает не 18, сборка бессмысленна):
```cmd
findstr /C:"MakeSpellArgs" "C:\TrinityCore\src\server\scripts\Spells\spell_paladin.cpp" | find /c /v ""
```
Должно напечатать **18** (16 мест + 2 строки хелпера). Если напечатало **0** — код не вставлен:
запустите `step1_scripts.bat` (сам вставит или заменит старую версию) и проверьте снова.
Напечатает **>0, но не 18** — пришлите мне вывод `findstr /C:"MakeSpellArgs" ...` без `| find`.

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

**⚠️ Если msbuild отработал за несколько секунд, а findstr печатает НЕТ**
(при этом проверка `MakeSpellArgs` выше даёт 18) — база отслеживания сборки
рассинхронизировалась (типично после неудачной сборки с ошибками). Форсируйте точечно
(в x64 Native Tools, worldserver должен быть закрыт; подставьте свой путь к ядру):
```cmd
cd /d C:\TrinityCore\build
del src\server\scripts\scripts.dir\Release\spell_paladin.obj
copy /b "C:\TrinityCore\src\server\scripts\Spells\spell_paladin.cpp"+,,
msbuild src\server\scripts\scripts.vcxproj /p:Configuration=Release /p:Platform=x64 /m
msbuild src\server\worldserver\worldserver.vcxproj /p:Configuration=Release /p:Platform=x64 /m
```
Первая сборка займёт несколько минут — в логе должна появиться строка
`spell_paladin.cpp`. После неё повторите findstr-проверку exe (должен найтись).


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

## Выгрузка ID заклинаний (аддон PalDump — для следующих партий)

Чтобы не собирать ID по одному, в `paladin\addons\PalDump` лежит мини-аддон.
Установка: скопируйте папку `PalDump` в `<клиент>\Interface\AddOns\`, в игре введите `/reload`.
Книга заклинаний в клиенте показывает **только активную спеку**, поэтому сделайте дамп
**в каждой специализации**: переключились в Свет → `/paldump` → `/reload`; то же в Воздаянии и Защите.
Аддон складывает все спеки в файл по отдельности (не затирает) и пишет, что уже сохранено.
Затем пришлите мне файл `<клиент>\WTF\Account\<имя аккаунта>\SavedVariables\PalDump.lua` —
в нём ID всех способностей каждой спеки + (если клиент даст) всех талантов дерева.
Если аддон «не совместим» — в окне выбора аддонов включите «Загружать устаревшие».

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


## 8. Фикс после краша #2 (зацикливание проков + Хаммер Ламповщика)

Что изменилось в скриптах (партии 6 и 7):
- наказание зла (b6): повторный каст теперь только при живой ауре и списывает стаки ДО ре-каста; triggered-рекасты больше не прокают сами себя.
- пробуждение Солнца (b7): triggered-рекасты не прокают повторно (ограничение цепочки одним звеном).
- Хаммер света Ламповщика (427453): хук переведён с эффект-индекса на OnHit — урон работал молча неработающим в старой версии.

### Шаг 1. Обновить скрипты
1. Скачай обновлённый ZIP этой ветки (Code -> Download ZIP).
2. Запусти `paladin\windows\step1_scripts.bat` (cmd) — скрипт сам найдёт старую версию и заменит её.
3. Жди строку `>>> найдена УСТАРЕВШАЯ версия партий` и затем `[X] ШАГ 1.1 ВЫПОЛНЕН (последняя версия)`.
   Проверка [3c]: `AT-скрипт молота: 1; анти-зацикливание: 2`.

### Шаг 2. Пересобрать ядро (только scripts + worldserver)
В "x64 Native Tools Command Prompt for VS 2022":
```
cd /d C:\BuildTrinity\TrinityCore-build
del /q src\server\scripts\CMakeFiles\scripts.dir\Spells\spell_paladin.cpp.obj
msbuild TrinityCore.sln /p:Configuration=Release /p:Platform=x64 /m /v:m /t:scripts
msbuild TrinityCore.sln /p:Configuration=Release /p:Platform=x64 /m /v:m /t:worldserver
```
Тишина/без ошибок = ОК. Жди `worldserver.vcxproj -> ...` в конце. Обе машины — одинаково.

### Шаг 3. Проверка
- Замени worldserver.exe на сервере, запусти, играй 30-60 мин всеми тремя спеками.
- Если краш ПОВТОРИТСЯ: пришли новый .dmp и точно укажи время краша, плюс вывод:
  `findstr /i "spell_pal" Server.log` (из папки сервера).
- В логе загрузки НЕ должно быть строки про `spell_pal_hammer_of_light_ex` и `did not match dbc effect data`
  (в старой версии было 11 таких предупреждений; эта версия чинит главную).

### Шаг 4. Боевой лог PalDump v4.1 (обязательно до следующей сессии)
1. Обнови аддона: `paladin\addons\PalDump` -> `<клиент>\Interface\AddOns\` (замена 2 файлов), `/reload`.
2. Всё, больше ничего: лог включён по умолчанию, пишет и вне боя, ID книги/талантов пишутся сами.
3. Когда увидишь прыгающие бафы (цикл храмовника): `/paldumplog show 30` — и пришли PalDump.lua.
   В логе будет строка «!!! ЦИКЛ? [ID] Имя» — этого достаточно, чтобы я вырезал цикл.
4. Играешь как обычно. Лог копится в памяти; файл на диск обновляется при `/reload`
   и при выходе из игры (принудительный автосейв `SaveVariables` каждые 10 сек убран
   в PalDump v4.1 — клиент 12.x запрещает аддонам так делать, попап «Модификация
   Paldump заблокирована…» был именно от него).
5. Если краш: НЕ убивай клиент задачей — сначала в игре `/reload` (или дождись отключения и выйди из игры штатно), тогда файл запишется, и только потом пришли `WTF\Account\<аккаунт>\SavedVariables\PalDump.lua` + новый .dmp.
6. Заодно для следующей партии: в КАЖДОЙ спеке выполни `/paldump` + `/reload` (Свет, Воздаяние, Защита)
   и пришли тот же файл — нужны ID книги заклинаний с ИГРОВОГО ПК.

## 9. Фикс прок-цикла «Божественный молот» по ретейлу (24.09.2026)

Причина найдена по твоим SQL-выдачам: пассивка 432929 «Божественный молот» имела
безусловный прок (шанс 101, без кулдауна) и кастовала молот 198034 с КАЖДОЙ
способности (в т.ч. маунт/предмет) → «3-4 бафа каждые 1-2 сек» + спам
«AreaTrigger 37932 not created». На ретейле (Midnight 12.x) проков с кастов НЕТ:
молот призывается ТОЛЬКО ТДА, висит 8с, тикает каждые 2с, и КАЖДАЯ потраченная
Сила Света продлевает его на +0.5 сек (warcraft.wiki, гайды Method/IcyVeins).

### Шаг 1. Обновить скрипты (изменилась партия 9)
1. Скачай свежий ZIP ветки (Code -> Download ZIP), запусти `paladin\windows\step1_scripts.bat` (cmd).
2. Проверка (cmd, из папки сборки):
```
findstr /c:"родной периодик" src\server\scripts\Spells\spell_paladin.cpp
findstr /c:"each Holy Power spent" src\server\scripts\Spells\spell_paladin.cpp
```
Каждая команда должна вывести ровно одну строку. Тишина = скрипты не обновились.

### Шаг 2. SQL — ВНИМАНИЕ, теперь ДВЕ базы
1. Открой `paladin\fix_432929_root.sql` и выполни его ДВЕ ЧАСТИ раздельно:
   - ЧАСТЬ 1 — в hotfix-БАЗЕ (где делал Q6/Q7): UPDATE отключает прок-флаги 432929
     у источника (ProcTypeMask (0,4)="Cast Successful" -> (0,0)). Грид: ProcTypeMask1=0, ProcTypeMask2=0.
   - ЧАСТЬ 2 — в базе world: REPLACE со страховочным Chance=0.01. Грид: Chance=0.01.
2. `paladin\areatrigger_37932_stub.sql` — заглушка пропсов AT 37932, в базе world
   (две строки, в обеих cnt=1). ВАЖНО: свежая версия файла (27 колонок под схему
   сборочного коммита a96d8977) — старая падала с "Unknown column".
Старые fix_432929_proc_icd.sql / fix_432929_retail.sql устарели: если ставил —
ничего не удаляй, ЧАСТЬ 2 перезапишет их строку.

### Шаг 3. Пересобрать ядро (только scripts + worldserver)
В "x64 Native Tools Command Prompt for VS 2022" — те же команды, что в разделе 8, Шаг 2
(del obj для spell_paladin.cpp -> msbuild scripts -> msbuild worldserver). Тишина = ОК.

### Шаг 4.1. Если DBErrors.log раздулся (сотни МБ) — свод вместо заливки
Файл пишется построчно с добавлением (append) и НЕ ротируется — копит ошибки всех
прошлых запусков, целиком передавать его НЕ нужно и некуда (лимит 25 МБ).
1. Возьми из ZIP `paladin\windows\dberrors_report.bat` (+ рядом лежащий .ps1).
2. Запусти bat (можно прямо перетащить DBErrors.log на него). Жди прогресс
   "обработано N строк" — на 875 МБ это несколько минут.
3. Получишь рядом с логом: `DBErrors_unique.txt` — уникальные ошибки со счётчиком
   (обычно сотни строк, килобайты), плюс head/tail по 100 строк.
4. Пришли мне `DBErrors_unique.txt` — по нему я назову точные исправления.
5. Сам DBErrors.log безопасно удалить/переименовать при ОСТАНОВЛЕННОМ сервере —
   это просто журнал.

### Шаг 4. Запуск и проверка
- МАРКЕР ФИКСА живёт НЕ в Server.log (логгер sql.sql пишется в DBErrors.log,
  worldserver.conf.dist: Logger.sql.sql=5,Console DBErrors). После РЕСТАРТА сервера:
  `findstr /i "ProcFlags" DBErrors.log` (и смотри консоль при старте) — ДОЛЖНА быть
  строка `spellId 432929 doesn't have any ProcFlags value defined, proc will not be
  triggered` = доказательство, что ЧАСТЬ 1 доехала. Если пусто: (а) сервер не
  перезапускался после SQL; (б) проверь контрольные SELECT из fix_432929_root.sql
  (hotfix: ProcTypeMask1/2=0; world: Chance=0.01) и пришли их вывод.
  [ПОДТВЕРЖДЕНО 24.09: строка найдена юзером в DBErrors.log — корневой фикс работает]
- Строк «AreaTrigger ... 37932 ... Invalid areatrigger create properties id» быть НЕ должно.
- В игре храмовником: молот появляется ТОЛЬКО после ТДА (375576); маунт/предмет/атаки бафы
  НЕ обновляют. Продление +0.5с за ОС снято 26.09 (тултип 12.x этого больше не говорит) — см. §11.
  если бафы всё ещё прыгают, пришли PalDump.lua (`/paldumplog show 30`).

## 10. Краш сервера на касте «Благословенный молот» (204019) — 25.09.2026

СИМПТОМ: worldserver падает (ACCESS_VIOLATION) ровно в момент каста 204019.

ПРИЧИНА (НАЙДЕНА) — НЕ ядро и НЕ spell_paladin.cpp, а битые ДАННЫЕ в world DB,
которые создала первая версия `paladin_class_fixes_8.sql`:
* строка `areatrigger_create_properties` (Id=6006) содержала **AreaTriggerId=0**;
* ядро (`AreaTriggerDataStore.cpp:193`) при AreaTriggerId=0 **пропускает** проверку
  шаблона и молча грузит `Template=nullptr`;
* первый же каст 204019 → `AreaTrigger::Create` → `IsServerSide()`
  (AreaTrigger.h:111, разыменование `_areaTriggerTemplate->Flags`) → NULL → КРАШ.
  Именно это видно в дампе 23.09 22:47: `IsServerSide+7 ← Create+6F5 ← CreateAreaTrigger+AF`.
* вторая ошибка того же файла: спиральные сплайн-точки не вставлялись
  (WHERE NOT EXISTS смотрел не на ту таблицу).

ЧТО ДЕЛАТЬ:
1. Выполни ПОВТОРНО в world DB: `paladin/paladin_class_fixes_8.sql` —
   файл обновлён и самоисцеляющийся (идемпотентен).
2. Проверка (обе строки обязательны):
   - `SELECT Id,AreaTriggerId,IsAreatriggerCustom,ScriptName FROM areatrigger_create_properties WHERE Id=6006;`
     → `AreaTriggerId` ДОЛЖЕН быть **6006** (не 0);
   - `SELECT COUNT(*) FROM areatrigger_create_properties_spline_point WHERE AreaTriggerCreatePropertiesId=6006;`
     → **49** (спираль на месте).
3. Перезапуск worldserver. В DBErrors.log НЕ должно быть строк:
   `references invalid AreaTrigger` и `AreaTriggerCreatePropertiesId (Id: 6006`.
4. Тест в игре: каст 204019 у манекена — сервер не падает, спираль летит, урон 204301.
5. Краш ПОВТОРИЛСЯ → пришли новый `.dmp`; в WinDbg по нему: `!analyze -v`
   (в дампах #2–#4 адрес краша символизировался спорно — сверим по настоящему pdb).

Примечание: «бафы кидаются с любой способности» — это ОТДЕЛЬНАЯ тема (прок-цикл),
с крашом не связана. Жду `PalDump.lua` с маркерами «!!! ЦИКЛ? / !!! АУРА-ШТОРМ» (Шаг 4).

## 11. Ревизия механик 26.09.2026 (`PAL_MECH_REV_20260926`)

Дампы `AUDIT_1_WORLD.sql` / `AUDIT_2_HOTFIXES.sql` больше не блокируют работу.
Как должны работать способности — в `paladin/MECHANICS_20260926.md`.
Код уже в партиях 6/7/9. На уже установленном сервере старый `step1` считал
партии свежими и ничего не переписывал. Теперь без маркера `PAL_MECH_REV_20260926`
он откатывает `spell_paladin.cpp` и вставляет заново.

Сделать по порядку:

1. Заново скачать папку `paladin/` (или подтянуть этот коммит).
2. Запустить `paladin\windows\step1_scripts.bat`. В логе должно быть
   `механика 26.09: True` и `ШАГ 1.1 ВЫПОЛНЕН`. Если написало «УЖЕ УСТАНОВЛЕНО»,
   а `check_all.bat` ругается на маркер — партии старые, step1 надо прогнать ещё раз
   уже с новым ps1.
3. В **world** заново выполнить (повтор безопасен):
   `paladin_class_fixes_5.sql`, `_6.sql`, `_9.sql`.
4. В **hotfixes** выполнить `MECH_PROC_FIX.sql`
   (обнуляет проки 432463 / 431533 / 425518 / 431687, иначе скрипт и DBC
   сработают оба; заодно ставит стаки баффа Рассвета 431522, если их меньше 2).
5. Пересобрать `scripts` + `worldserver`, перезапустить мир.
6. `check_all.bat` — строка «механика 26.09 на месте» должна быть зелёной.

Что изменилось в игре (коротко):

- Молотопад — с Приговора/Бури (рет) и Щита праведника/Слова света (прот), не с Молота Света.
- Сотрясение небес обновляет только Молот Света. Билдеры продлевают его только с Высшим призванием.
- Божественный молот — 8с от Звона, без +0.5с за Силу Света.
- Избавление Света копится молотками и съедается на 60, когда и генератор, и кнопка Молота недоступны.
- Неоспоримое постановление больше не кастует полный Правосудие/Щит праведника (не тратит СС и не плодит чужие проки).
- Рассветный свет выдаётся Пробуждением зол (рет) или Призмой/Звоном (холи), а тратится спендером СС. Правосудие и Шок его больше не вешают.
- Солнечный ожог — крит Молота гнева или Бури (только рет).
