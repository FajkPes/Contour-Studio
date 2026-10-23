@echo off
setlocal
if "%~1"=="" (
  echo Usage: Capture.bat "image.png" "output-folder" -Analysis 1024 -Mode edges -Points 16
  echo You can also drag an image onto this file.
  exit /b 2
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0dev\capture.ps1" %*
exit /b %errorlevel%
