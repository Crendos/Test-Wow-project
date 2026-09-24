@echo off
rem Launcher (PowerShell inside). English-only to avoid codepage issues.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0step1_scripts.ps1"
