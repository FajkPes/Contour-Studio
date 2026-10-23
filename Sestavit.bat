@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0dev\build.ps1" -Publish
if errorlevel 1 (
  echo Sestaveni se nezdarilo. Chyba je vypsana vyse.
  pause
  exit /b 1
)
echo Hotovo. Aplikaci zapnete souborem Spustit.bat.
