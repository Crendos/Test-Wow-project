@echo off
rem English-only. Stamp a UTF-8 BOM so Windows PowerShell 5.1 does not
rem misread Russian text in check_all.ps1.
set "PAL_SCRIPT=%~dp0check_all.ps1"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$p=$env:PAL_SCRIPT; $b=[IO.File]::ReadAllBytes($p); if ($b.Length -lt 3 -or $b[0] -ne 239 -or $b[1] -ne 187 -or $b[2] -ne 191) { $t=[IO.File]::ReadAllText($p, (New-Object System.Text.UTF8Encoding $false)); [IO.File]::WriteAllText($p, $t, (New-Object System.Text.UTF8Encoding $true)) }"
if errorlevel 1 (
    echo [X] could not prepare check_all.ps1
    pause
    exit /b 1
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%PAL_SCRIPT%"
