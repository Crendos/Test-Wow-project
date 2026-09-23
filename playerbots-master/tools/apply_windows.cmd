@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion
rem ==========================================================================
rem PLAYERBOT MASTER — применение патча ядра + модуля и сборка на Windows.
rem Запускать ТОЛЬКО из "x64 Native Tools Command Prompt for VS 2022".
rem
rem Использование:
rem   apply_windows.cmd <путь\к\исходникам\TrinityCore> <путь\к\build\папке> [конфиг]
rem Пример:
rem   apply_windows.cmd C:\TrinityCore\Source C:\TrinityCore\Build RelWithDebInfo
rem ==========================================================================

if not defined VisualStudioVersion (
    echo [ОШИБКА] Скрипт запущен не из "x64 Native Tools Command Prompt for VS 2022".
    echo          Откройте: Пуск -^> Visual Studio 2022 -^> x64 Native Tools Command Prompt
    exit /b 1
)

set "MODDIR=%~dp0.."
set "SRC=%~1"
set "BUILD=%~2"
set "CONFIG=%~3"
if "%CONFIG%"=="" set "CONFIG=RelWithDebInfo"

if "%SRC%"==""  set /p SRC=Путь к исходникам TrinityCore: 
if "%BUILD%"=="" set /p BUILD=Путь к папке сборки ^(где CMakeCache.txt^): 

if not exist "%SRC%\CMakeLists.txt" ( echo [ОШИБКА] Не похоже на исходник TrinityCore: %SRC%& exit /b 1 )
if not exist "%SRC%\src\server\game\Server\WorldSession.h" ( echo [ОШИБКА] Не найден WorldSession.h в %SRC%& exit /b 1 )
if not exist "%BUILD%\CMakeCache.txt" ( echo [ОШИБКА] Не настроенная build-папка: %BUILD% ^(сначала cmake -G "Visual Studio 17 2022" -A x64 ..^)& exit /b 1 )

echo ==========================================================================
echo [1/6] Ядро: %SRC%
echo       Сборка: %BUILD%
echo       Модуль: %MODDIR%
echo ==========================================================================

rem --- 1. Ветка/чистота дерева -------------------------------------------
pushd "%SRC%"
for /f %%i in ('git status --porcelain -- src/server/game/Server/WorldSession.h src/server/game/Server/WorldSession.cpp src/server/game/Handlers/CharacterHandler.cpp src/server/scripts/Custom/custom_script_loader.cpp') do (
    echo [ВНИМАНИЕ] В ядре есть локальные правки пересекающихся файлов — патч может лечь с конфликтом.
)
echo [2/6] Применяю patches\0001-core-integration.diff ...
git apply --3way --verbose "%MODDIR%\patches\0001-core-integration.diff"
if errorlevel 1 (
    echo [ОШИБКА] git apply --3way не смог применить патч.
    echo          См. правку вручную по docs\02_CORE_PATCH.md ^(фрагменты мелкие^),
    echo          либо: git apply --reject "%MODDIR%\patches\0001-core-integration.diff"
    echo          и разобрать *.rej файлы.
    popd
    exit /b 1
)

rem --- 2. Копия файлов модуля ---------------------------------------------
echo [3/6] Копирую модуль в src\server\scripts\Custom\playerbots ...
if not exist "%SRC%\src\server\scripts\Custom\playerbots" mkdir "%SRC%\src\server\scripts\Custom\playerbots"
robocopy "%MODDIR%\src\bot" "%SRC%\src\server\scripts\Custom\playerbots" *.h *.cpp /NFL /NDL /NJH /NJS >nul
if errorlevel 8 ( echo [ОШИБКА] robocopy вернул ошибку & popd & exit /b 1 )

rem --- 3. Переконфигурация CMake (кеш с вашими путями сохраняется) --------
echo [4/6] Rerun cmake ^(подхватываются новые .cpp; пути к Boost/OpenSSL остаются вашими^)...
cmake "%BUILD%"
if errorlevel 1 ( echo [ОШИБКА] cmake reconfigure упал & popd & exit /b 1 )

rem --- 4. Сборка затронутых целей ------------------------------------------
echo [5/6] Собираю game ^(патч WorldSession/CharacterHandler^) — займёт время...
cmake --build "%BUILD%" --config %CONFIG% --target game -- /m
if errorlevel 1 ( echo [ОШИБКА] Сборка проекта game & popd & exit /b 1 )

echo [5/6] Собираю scripts_custom ^(модуль playerbots — ваш SCRIPTS=dynamic^)...
cmake --build "%BUILD%" --config %CONFIG% --target scripts_custom -- /m
if errorlevel 1 ( echo [ОШИБКА] Сборка scripts_custom & popd & exit /b 1 )

echo [5/6] Собираю worldserver ^(перелинковка = game.dll изменилась^)...
cmake --build "%BUILD%" --config %CONFIG% --target worldserver -- /m
if errorlevel 1 ( echo [ОШИБКА] Сборка worldserver & popd & exit /b 1 )

rem --- 5. Остаток руками ----------------------------------------------------
echo [6/6] Ручной остаток ^(1 раз^):
echo   1^) Мир: через ваш MySQL-клиент выполнить sql\world_playerbots_rotation.sql
echo         ^(создаст world.playerbots_rotation + world.playerbots_combat_spells^).
echo   2^) В worldserver.conf добавить ключи из conf\playerbots.conf.dist
echo         ^(минимум: Playerbots.Enabled = 1^).
echo   3^) scripts_custom.dll положите рядом с worldserver.exe ^(при SCRIPTS=dynamic
echo         CMake сам кладёт её в выходную папку build\bin\%CONFIG%^).
echo.
echo ГОТОВО: патч применён, модуль на месте, проекты собраны.
popd
endlocal
