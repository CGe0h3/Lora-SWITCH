@echo off
title Heltec V4 - Moniteur serie
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\windows\moniteur.ps1" %*
pause
