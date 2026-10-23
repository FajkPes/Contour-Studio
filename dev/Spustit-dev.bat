@echo off
setlocal
if not exist "%~dp0build\Release\ContourStudio.exe" (
  echo Chybi dev sestaveni. Spustte Sestavit.bat v hlavni slozce.
  pause
  exit /b 1
)
start "" "%~dp0build\Release\ContourStudio.exe" %*
