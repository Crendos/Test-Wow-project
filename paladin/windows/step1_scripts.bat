@echo off
rem English-only. Windows PowerShell 5.1 reads a .ps1 as ANSI unless the file
rem has a UTF-8 BOM. Russian text then breaks the parser. Stamp the BOM first.
set "PAL_SCRIPT=%~dp0step1_scripts.ps1"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$p=$env:PAL_SCRIPT; $b=[IO.File]::ReadAllBytes($p); if ($b.Length -lt 3 -or $b[0] -ne 239 -or $b[1] -ne 187 -or $b[2] -ne 191) { $t=[IO.File]::ReadAllText($p, (New-Object System.Text.UTF8Encoding $false)); [IO.File]::WriteAllText($p, $t, (New-Object System.Text.UTF8Encoding $true)) }"
if errorlevel 1 (
    echo [X] could not prepare step1_scripts.ps1
    pause
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%PAL_SCRIPT%"
