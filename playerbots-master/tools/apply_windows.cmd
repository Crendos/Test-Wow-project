@echo off
setlocal EnableDelayedExpansion
rem ==========================================================================
rem  PLAYERBOTS master - apply core patch + module (Windows, PATCH ONLY)
rem  RUN ONLY from "x64 Native Tools Command Prompt for VS 2022".
rem
rem  This script does NOT compile anything. Workflow:
rem    1) apply_windows.cmd <TC_SOURCE_DIR>   - patch core + copy module
rem    2) your own source fixes (optional, e.g. class/paladin fixes)
rem    3) build_windows.cmd <TC_BUILD_DIR>    - compile everything together
rem
rem  Usage:
rem    apply_windows.cmd <TC_SOURCE_DIR>
rem  Example:
rem    apply_windows.cmd C:\TrinityCore\Source
rem ==========================================================================

if not defined VisualStudioVersion (
    echo [ERROR] Run this from "x64 Native Tools Command Prompt for VS 2022".
    exit /b 1
)

set "MODDIR=%~dp0.."
set "SRC=%~1"

if "%SRC%"==""  set /p "SRC=Path to TrinityCore source: "

if not exist "%SRC%\CMakeLists.txt" ( echo [ERROR] Not a TrinityCore source dir: %SRC%& exit /b 1 )
if not exist "%SRC%\src\server\game\Server\WorldSession.h" ( echo [ERROR] WorldSession.h not found in %SRC%& exit /b 1 )

echo ==========================================================================
echo [1/4] Source : %SRC%
echo       Module : %MODDIR%
echo       Mode   : PATCH ONLY ^(no build^)
echo ==========================================================================

pushd "%SRC%"

echo [2/4] git apply --3way patches/0001-core-integration.diff ...
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

echo [3/4] Copy bot module to src\server\scripts\Custom\playerbots ...
if not exist "%SRC%\src\server\scripts\Custom\playerbots" mkdir "%SRC%\src\server\scripts\Custom\playerbots"
robocopy "%MODDIR%\src\bot" "%SRC%\src\server\scripts\Custom\playerbots" *.h *.cpp /NFL /NDL /NJH /NJS >nul
if errorlevel 8 ( echo [ERROR] robocopy failed & popd & exit /b 1 )

echo [4/4] Done. No build was started.
echo.
echo Next steps:
echo   1. Add your own source fixes if needed, e.g.:
echo        %SRC%\src\server\scripts\Custom\playerbots  (module files)
echo        %SRC%\src\server\game\...                   (core files)
echo      WARNING: re-running this script OVERWRITES files copied from
echo      src\bot - keep custom fixes in files that do not exist upstream,
echo      or commit them to the playerbots-master repo.
echo   2. Compile everything together:
echo        "%MODDIR%\tools\build_windows.cmd" ^<TC_BUILD_DIR^> [CONFIG]
echo   3. One-time data/config:
echo      - sql\world_playerbots_rotation.sql into world database
echo        (see docs\08 for the other SQL files);
echo      - keys from conf\playerbots.conf.dist into worldserver.conf
echo        (minimum: Playerbots.Enabled = 1).
popd
endlocal
