@echo off
setlocal EnableDelayedExpansion
rem ==========================================================================
rem  PLAYERBOTS master - apply core patch + module, then build (Windows)
rem  RUN ONLY from "x64 Native Tools Command Prompt for VS 2022".
rem
rem  Usage:
rem    apply_windows.cmd <TC_SOURCE_DIR> <TC_BUILD_DIR> [CONFIG]
rem  Example:
rem    apply_windows.cmd C:\TrinityCore\Source C:\TrinityCore\Build RelWithDebInfo
rem ==========================================================================

if not defined VisualStudioVersion (
    echo [ERROR] Run this from "x64 Native Tools Command Prompt for VS 2022".
    exit /b 1
)

set "MODDIR=%~dp0.."
set "SRC=%~1"
set "BUILD=%~2"
set "CONFIG=%~3"
if "%CONFIG%"=="" set "CONFIG=RelWithDebInfo"

if "%SRC%"==""  set /p "SRC=Path to TrinityCore source: "
if "%BUILD%"=="" set /p "BUILD=Path to build dir with CMakeCache.txt: "

if not exist "%SRC%\CMakeLists.txt" ( echo [ERROR] Not a TrinityCore source dir: %SRC%& exit /b 1 )
if not exist "%SRC%\src\server\game\Server\WorldSession.h" ( echo [ERROR] WorldSession.h not found in %SRC%& exit /b 1 )
if not exist "%BUILD%\CMakeCache.txt" ( echo [ERROR] Build dir not configured: %BUILD% ^(run: cmake -G "Visual Studio 17 2022" -A x64 ..^)& exit /b 1 )

echo ==========================================================================
echo [1/6] Source : %SRC%
echo       Build  : %BUILD%
echo       Module : %MODDIR%
echo       Config : %CONFIG%
echo ==========================================================================

pushd "%SRC%"

echo [2/6] git apply --3way patches/0001-core-integration.diff ...
git apply --3way --verbose "%MODDIR%\patches\0001-core-integration.diff"
if errorlevel 1 (
    git apply --reverse --quiet --check "%MODDIR%\patches\0001-core-integration.diff"
    if not errorlevel 1 (
        echo [INFO] Patch is already applied - skipping.
    ) else (
        echo [ERROR] git apply failed. Manual steps:
        echo         1. git apply --reject "%MODDIR%\patches\0001-core-integration.diff"
        echo         2. Resolve .rej files using docs\02_CORE_PATCH.md as reference.
        popd
        exit /b 1
    )
)

echo [3/6] Copy bot module to src\server\scripts\Custom\playerbots ...
if not exist "%SRC%\src\server\scripts\Custom\playerbots" mkdir "%SRC%\src\server\scripts\Custom\playerbots"
robocopy "%MODDIR%\src\bot" "%SRC%\src\server\scripts\Custom\playerbots" *.h *.cpp /NFL /NDL /NJH /NJS >nul
if errorlevel 8 ( echo [ERROR] robocopy failed & popd & exit /b 1 )

echo [4/6] cmake reconfigure ^(picks up new files; your cached paths stay^) ...
cmake "%BUILD%"
if errorlevel 1 ( echo [ERROR] cmake reconfigure failed & popd & exit /b 1 )

echo [5/6] Build game ...
cmake --build "%BUILD%" --config %CONFIG% --target game -- /m
if errorlevel 1 ( echo [ERROR] build target game failed & popd & exit /b 1 )

rem --- detect scripts mode from CMakeCache (static -> target "scripts", dynamic -> "scripts_custom") ---
set "SCRIPTS_TARGET=scripts"
findstr /R /C:"^SCRIPTS:STRING=dynamic" "%BUILD%\CMakeCache.txt" >nul 2>nul
if not errorlevel 1 set "SCRIPTS_TARGET=scripts_custom"

echo [5/6] Build %SCRIPTS_TARGET% ...
cmake --build "%BUILD%" --config %CONFIG% --target %SCRIPTS_TARGET% -- /m
if errorlevel 1 ( echo [ERROR] build target %SCRIPTS_TARGET% failed & popd & exit /b 1 )

echo [5/6] Build worldserver ...
cmake --build "%BUILD%" --config %CONFIG% --target worldserver -- /m
if errorlevel 1 ( echo [ERROR] build target worldserver failed & popd & exit /b 1 )

echo [6/6] Done. One-time manual steps:
echo   1. Run sql\world_playerbots_rotation.sql into your world database.
echo   2. Add keys from conf\playerbots.conf.dist to worldserver.conf
echo      ^(minimum: Playerbots.Enabled = 1^).
echo   3. If your build is SCRIPTS=dynamic: make sure scripts_custom.dll is next
echo      to worldserver.exe in build\bin\%CONFIG%. ^(Static mode: nothing extra.^)
echo.
echo ALL OK: patch applied, module installed, targets built.
popd
endlocal
