@echo off
setlocal
cd /d "%~dp0.."
call tests\setup-msvc.cmd
if errorlevel 1 exit /b 1
python tests\ui_value_lifetime.py
exit /b %errorlevel%
