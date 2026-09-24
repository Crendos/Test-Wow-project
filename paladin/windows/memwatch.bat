@echo off
rem Мониторинг памяти worldserver/MySQL -> memwatch.csv (каждые 30 с, Ctrl+C = стоп)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0memwatch.ps1"
pause
