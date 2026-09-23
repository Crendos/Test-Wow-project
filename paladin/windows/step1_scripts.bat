@echo off
REM ============================================================================
REM Паладин-фиксы — ШАГ 1 полностью в консоли Windows (без Git Bash).
REM Запуск из x64 Native Tools Command Prompt for VS 2022 ИЛИ обычного cmd:
REM
REM   "C:\paladin-fixes\paladin\windows\step1_scripts.bat" "C:\paladin-fixes\paladin" "C:\TrinityCore"
REM
REM (1-й аргумент — папка с фиксами, 2-й — корень ядра; можно поменять на свои)
REM ============================================================================
setlocal enabledelayedexpansion

set "FIX=%~1"
if "%FIX%"=="" set "FIX=C:\paladin-fixes\paladin"
set "CORE=%~2"
if "%CORE%"=="" set "CORE=C:\TrinityCore"
set "PAL_CPP=%CORE%\src\server\scripts\Spells\spell_paladin.cpp"
set "LOADER=%CORE%\src\server\scripts\Spells\spell_script_loader.cpp"

echo === [0/4] Проверяю пути ===
if not exist "%PAL_CPP%" ( echo >>> ОШИБКА: не найден "%PAL_CPP%" & exit /b 1 )
if not exist "%LOADER%"   ( echo >>> ОШИБКА: не найден "%LOADER%" & exit /b 1 )
if not exist "%FIX%\spell_paladin_class_fixes.cpp" ( echo >>> ОШИБКА: нет папки фиксов "%FIX%" & exit /b 1 )
echo >>> OK: файлы найдены

echo === [1/4] Считаю партии в spell_paladin.cpp ===
set N=0
for /f %%c in ('findstr /c:"// === CUT HERE" "%PAL_CPP%" ^| find /c /v ""') do set N=%%c
echo >>> сейчас партий: %N% (нужно 0 или 10)
if "%N%"=="10" ( echo >>> УЖЕ УСТАНОВЛЕНО — шаг 1.1 пропущен & goto loader )
if not "%N%"=="0" (
    echo >>> файл заполнен частично/задвоенно — восстанавливаю из git...
    cd /d "%CORE%"
    git checkout -- "src/server/scripts/Spells/spell_paladin.cpp" 2>nul
    set N=0
)

echo === [2/4] Дописываю 10 файлов ^(хвост каждого файла, от маркера CUT HERE^) ===
set MARK=
for %%f in ("%FIX%\spell_paladin_class_fixes.cpp" "%FIX%\spell_paladin_class_fixes_2.cpp" "%FIX%\spell_paladin_class_fixes_3.cpp" "%FIX%\spell_paladin_class_fixes_4.cpp" "%FIX%\spell_paladin_class_fixes_5.cpp" "%FIX%\spell_paladin_class_fixes_6.cpp" "%FIX%\spell_paladin_class_fixes_7.cpp" "%FIX%\spell_paladin_class_fixes_8.cpp" "%FIX%\spell_paladin_class_fixes_9.cpp" "%FIX%\spell_paladin_class_fixes_10.cpp") do (
    REM находим номер строки маркера "// === CUT HERE" в файле
    set LN=
    for /f "delims=" %%i in ('findstr /n /c:"// === CUT HERE" %%f') do set "LN=%%i"
    set LN=!LN:*:=!
    if "!LN!"=="" ( echo >>> ОШИБКА: маркер не найден в %%f & exit /b 1 )
    REM дописываем содержимое ПОСЛЕ строки маркера
    more +!LN! %%f >> "%PAL_CPP%"
    echo     добавлен: %%~nxf
)

echo === [3/4] Контроль партий ===
set N=0
for /f %%c in ('findstr /c:"// === CUT HERE" "%PAL_CPP%" ^| find /c /v ""') do set N=%%c
echo >>> партий в файле: %N% (нужно 10)
if not "%N%"=="10" ( echo >>> ЧТО-ТО НЕ ТАК — пришлите этот вывод целиком & exit /b 1 )
echo >>> ШАГ 1.1 ВЫПОЛНЕН

:loader
echo === [4/4] Лоадер spell_script_loader.cpp ===
findstr /c:"void AddSC_paladin_spell_scripts_ex10();" "%LOADER%" >nul && (
    echo >>> объявления уже есть — 1.2а пропущено
) || (
    echo Добавьте вручную в Notepad++ (блоки 1.2а и 1.2б из INSTALL.md^):
    echo   после строки "void AddSC_paladin_spell_scripts();"      - 9 строк void AddSC_..._ex2..ex10^(^;
    echo   после строки "    AddSC_paladin_spell_scripts();"       - 9 строк вызовов
    echo ^(автовставку лоадера пропускаю: правки точечные, безопаснее руками^)
)

echo.
echo === ИТОГ ШАГА 1 ===
findstr /c:"AddSC_paladin_spell_scripts_ex10" "%LOADER%" >nul && (echo >>> лоадер: OK) || (echo >>> лоадер: ДОПИШИТЕ 1.2а/1.2б вручную)
findstr /c:"AddSC_paladin_spell_scripts_ex10()" "%PAL_CPP%" >nul && (echo >>> партии: OK, ex10 внутри) || (echo >>> партии: ОШИБКА)
echo === Конец. Если оба OK — переходите к ШАГУ 2 (SQL) ===
endlocal
