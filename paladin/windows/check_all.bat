@echo off
REM ============================================================================
REM Паладин-фиксы — ОБЩАЯ ПРОВЕРКА (шаги 0–1 + сборка) в консоли Windows.
REM Запуск из любого cmd:
REM
REM   "C:\paladin-fixes\paladin\windows\check_all.bat" "C:\paladin-fixes\paladin" "C:\TrinityCore"
REM ============================================================================
setlocal enabledelayedexpansion
set "FIX=%~1"
if "%FIX%"=="" set "FIX=C:\paladin-fixes\paladin"
set "CORE=%~2"
if "%CORE%"=="" set "CORE=C:\TrinityCore"
set FAIL=0

echo === Шаг 0: патч ядра ===
findstr /c:"spellBlockChance" "%CORE%\src\server\game\Entities\Unit\Unit.cpp" >nul && (echo >>> OK: Unit.cpp пропатчен) || (echo >>> НЕТ: Unit.cpp без патча & set FAIL=1)
findstr /c:"HandleNoImmediateEffect" "%CORE%\src\server\game\Spells\Auras\SpellAuraEffects.cpp" >nul && (echo >>> OK: 529 заменён) || (echo >>> НЕТ: 529 не заменён & set FAIL=1)

echo === Шаг 1.1: партии ===
set N=0
for /f %%c in ('findstr /c:"// === CUT HERE" "%CORE%\src\server\scripts\Spells\spell_paladin.cpp" ^| find /c /v ""') do set N=%%c
if "%N%"=="10" (echo >>> OK: партий 10) else (echo >>> НЕТ: партий %N% (нужно 10) & set FAIL=1)

echo === Шаг 1.2: лоадер ===
set A=0
for /f %%c in ('findstr /c:"void AddSC_paladin_spell_scripts_ex" "%CORE%\src\server\scripts\Spells\spell_script_loader.cpp" ^| find /c /v ""') do set A=%%c
if "%A%"=="9" (echo >>> OK: объявлений 9) else (echo >>> НЕТ: объявлений %A% (нужно 9) & set FAIL=1)
set B=0
for /f %%c in ('findstr /c:"AddSC_paladin_spell_scripts_ex2();" "%CORE%\src\server\scripts\Spells\spell_script_loader.cpp" ^| find /c /v ""') do set B=%%c
if "%B%"=="1" (echo >>> OK: вызовы на месте (ex2 найден) ) else (echo >>> НЕТ: вызовы не добавлены & set FAIL=1)

echo === Шаг 3: сборка (если exe есть) ===
if exist "%CORE%\build\bin\Release\worldserver.exe" (
    findstr /m /c:"spell_pal_glory_of_the_vanguard_ex" "%CORE%\build\bin\Release\worldserver.exe" >nul && (echo >>> OK: скрипты внутри worldserver.exe) || (echo >>> НЕТ: exe без скриптов — пересоберите & set FAIL=1)
) else ( echo — exe не найден, пропускаю )

if "%FAIL%"=="0" (echo === ВСЁ СХОДИТСЯ: шаги 0-1 выполнены ===) else (echo === ЕСТЬ РАСХОЖДЕНИЯ — пришлите этот вывод целиком ===)
endlocal
