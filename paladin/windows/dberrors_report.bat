@echo off
chcp 65001 >nul
echo === DBErrors report ===
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0dberrors_report.ps1" %1
echo.
pause
