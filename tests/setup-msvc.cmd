@echo off
if defined VSCMD_VER exit /b 0
set "H1MVM_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%H1MVM_VSWHERE%" exit /b 1
for /f "usebackq tokens=*" %%i in (`"%H1MVM_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "H1MVM_VS=%%i"
if not defined H1MVM_VS exit /b 1
call "%H1MVM_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
exit /b %errorlevel%
