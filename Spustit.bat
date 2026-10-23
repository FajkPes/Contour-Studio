@echo off
setlocal
if not exist "%~dp0release\latest.txt" goto missing
set /p APP_VERSION=<"%~dp0release\latest.txt"
if not exist "%~dp0release\%APP_VERSION%\ContourStudio.exe" goto missing
start "" "%~dp0release\%APP_VERSION%\ContourStudio.exe" %*
exit /b 0
:missing
echo Chybi sestavena aplikace. Spustte nejdrive Sestavit.bat.
pause
exit /b 1
