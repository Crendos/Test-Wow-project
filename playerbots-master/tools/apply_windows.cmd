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
rem  git is optional: if git is not in PATH, the script checks a patch
rem  marker (MarkAsPlayerBot in WorldSession.h) with findstr and skips
rem  the patch step when the patch is already applied.
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

echo [2/4] Core patch: patches\0001-core-integration.diff ...

set "HAVE_GIT=1"
where git >nul 2>nul
if errorlevel 1 set "HAVE_GIT=0"

if "%HAVE_GIT%"=="0" goto :patch_no_git

git apply --3way --verbose "%MODDIR%\patches\0001-core-integration.diff"
if errorlevel 1 goto :patch_git_failed
goto :patch_done

:patch_no_git
rem --- git is not available: check our patch marker with built-in findstr ---
findstr /C:"MarkAsPlayerBot" "%SRC%\src\server\game\Server\WorldSession.h" >nul 2>nul
if errorlevel 1 (
    echo [ERROR] git not found in PATH and the core patch is NOT applied yet.
    echo         Fix - install Git for Windows: https://git-scm.com/download/win
    echo         ^(re-open this prompt after install; you also need git for
    echo         updating playerbots-master via git pull^)
    echo         Or apply the patch manually:
    echo           git apply --reject "%MODDIR%\patches\0001-core-integration.diff"
    echo         and resolve .rej files using docs\02_CORE_PATCH.md.
    popd
    exit /b 1
)
echo [INFO] git not found in PATH - patch marker detected (already applied), skipping patch step.
goto :patch_done

:patch_git_failed
git apply --reverse --quiet --check "%MODDIR%\patches\0001-core-integration.diff"
if not errorlevel 1 (
    echo [INFO] Patch is already applied - skipping.
    goto :patch_done
)
echo [ERROR] git apply failed. Manual steps:
echo         1. git apply --reject "%MODDIR%\patches\0001-core-integration.diff"
echo         2. Resolve .rej files using docs\02_CORE_PATCH.md as reference.
popd
exit /b 1

:patch_done

echo [3/4] Copy bot module to src\server\scripts\Custom\playerbots ...
if not exist "%SRC%\src\server\scripts\Custom\playerbots" mkdir "%SRC%\src\server\scripts\Custom\playerbots"
robocopy "%MODDIR%\src\bot" "%SRC%\src\server\scripts\Custom\playerbots" *.h *.cpp /NFL /NDL /NJH /NJS >nul
if errorlevel 8 ( echo [ERROR] robocopy failed & popd & exit /b 1 )
rem Sanity: every .cpp expected by the build must exist in the source module
if not exist "%MODDIR%\src\bot\DummyLog.cpp" (
  echo [ERROR] DummyLog.cpp is missing in "%MODDIR%\src\bot".
  echo         Your playerbots-master is stale - run git pull, or download
  echo         the branch ZIP and re-run this script.
  popd
  exit /b 1
)
if not exist "%SRC%\src\server\scripts\Custom\playerbots\DummyLog.cpp" (
  echo [ERROR] DummyLog.cpp was not copied into the TC source.
  popd
  exit /b 1
)

echo [4/4] Done. No build was started.
echo.
echo Next steps:
echo   1. Add your own source fixes if needed, e.g.:
echo        %SRC%\src\server\scripts\Custom\playerbots  (module files)
echo        %SRC%\src\server\game\...                   (core files)
echo      WARNING: re-running this script OVERWRITES files copied from
echo      src\bot - keep custom fixes in files that do not exist upstream,
echo      or commit them to the playerbots-master repo.
echo   2. NEW .cpp files in the module need a CMake re-configure first:
echo      sources are collected with file(GLOB) AT CONFIGURE TIME, so a
echo      build alone will not see files added later (LNK2019 symptoms).
echo      Re-run configuration (cache/generator are kept):
echo        cmake -B ^<TC_BUILD_DIR^> -S ^<TC_SRC^>
echo   3. Compile everything together:
echo        "%MODDIR%\tools\build_windows.cmd" ^<TC_BUILD_DIR^> [CONFIG]
echo   4. One-time data/config:
echo      - sql\world_playerbots_rotation.sql into world database
echo        (see docs\08 for the other SQL files);
echo      - keys from conf\playerbots.conf.dist into worldserver.conf
echo        (minimum: Playerbots.Enabled = 1).
popd
endlocal
