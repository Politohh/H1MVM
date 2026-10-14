@echo off
setlocal
if "%~1"=="" (
  echo Usage: Launch-SP-Bots.cmd "path to Modern Warfare Remastered"
  pause
  exit /b 1
)
powershell.exe -NoProfile -Command "if (Get-Process -Name h1-mod -ErrorAction SilentlyContinue) { exit 1 }"
if errorlevel 1 (
  echo Close H1 before starting this separate SP Bots preview.
  pause
  exit /b 1
)
if not exist "%~1\h1_mp64_ship.exe" (
  echo The selected directory does not contain h1_mp64_ship.exe.
  pause
  exit /b 1
)
cd /d "%~1"
if errorlevel 1 exit /b 1
if not exist "spai-diagnostics" mkdir "spai-diagnostics"
start "SP Bots WIP" "%~dp0h1-mod.exe" -multiplayer -spai-probe -bundled-only +set sv_lanOnly 1
