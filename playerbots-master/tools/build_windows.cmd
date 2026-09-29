@echo off
setlocal EnableDelayedExpansion
rem ==========================================================================
rem  PLAYERBOTS master - configure + build (Windows)
rem  RUN ONLY from "x64 Native Tools Command Prompt for VS 2022".
rem
rem  Run AFTER tools\apply_windows.cmd (and any of your own source fixes):
rem  reconfigures CMake (picks up new/changed files, cached paths stay) and
rem  builds: game -> scripts (or scripts_custom) -> worldserver.
rem
rem  Usage:
rem    build_windows.cmd <TC_BUILD_DIR> [CONFIG]
rem  Example:
rem    build_windows.cmd C:\TrinityCore\Build RelWithDebInfo
rem ==========================================================================

if not defined VisualStudioVersion (
    echo [ERROR] Run this from "x64 Native Tools Command Prompt for VS 2022".
    exit /b 1
)

set "BUILD=%~1"
set "CONFIG=%~2"
if "%CONFIG%"=="" set "CONFIG=RelWithDebInfo"

if "%BUILD%"=="" set /p "BUILD=Path to build dir with CMakeCache.txt: "

if not exist "%BUILD%\CMakeCache.txt" ( echo [ERROR] Build dir not configured: %BUILD% ^(run: cmake -G "Visual Studio 17 2022" -A x64 ..^)& exit /b 1 )

echo ==========================================================================
echo [1/5] Build  : %BUILD%
echo       Config : %CONFIG%
echo ==========================================================================

pushd "%BUILD%"

echo [2/5] cmake reconfigure ^(picks up new files; your cached paths stay^) ...
cmake "%BUILD%"
if errorlevel 1 ( echo [ERROR] cmake reconfigure failed & popd & exit /b 1 )

echo [3/5] Build game ...
cmake --build "%BUILD%" --config %CONFIG% --target game -- /m
if errorlevel 1 ( echo [ERROR] build target game failed & popd & exit /b 1 )

rem --- detect scripts mode from CMakeCache (static -> target "scripts", dynamic -> "scripts_custom") ---
set "SCRIPTS_TARGET=scripts"
findstr /R /C:"^SCRIPTS:STRING=dynamic" "%BUILD%\CMakeCache.txt" >nul 2>nul
if not errorlevel 1 set "SCRIPTS_TARGET=scripts_custom"

echo [4/5] Build %SCRIPTS_TARGET% ...
cmake --build "%BUILD%" --config %CONFIG% --target %SCRIPTS_TARGET% -- /m
if errorlevel 1 ( echo [ERROR] build target %SCRIPTS_TARGET% failed & popd & exit /b 1 )

echo [4/5] Build worldserver ...
cmake --build "%BUILD%" --config %CONFIG% --target worldserver -- /m
if errorlevel 1 ( echo [ERROR] build target worldserver failed & popd & exit /b 1 )

echo [5/5] Done. One-time manual steps:
echo   1. Run sql\world_playerbots_rotation.sql into your world database
echo      ^(plus the other SQL files from docs\08 as needed^).
echo   2. Add keys from conf\playerbots.conf.dist to worldserver.conf
echo      ^(minimum: Playerbots.Enabled = 1^).
echo   3. If your build is SCRIPTS=dynamic: make sure scripts_custom.dll is next
echo      to worldserver.exe in build\bin\%CONFIG%. ^(Static mode: nothing extra.^)
echo.
echo ALL OK: configured and built game + %SCRIPTS_TARGET% + worldserver.
popd
endlocal
