@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client tests\cod4_actor_catalog.cpp /Fe:build\cod4_actor_catalog.exe /Fo:build\cod4_actor_catalog.obj
if errorlevel 1 exit /b 1
build\cod4_actor_catalog.exe tests\fixtures\mwr-native-actor-codecs.tsv docs\cod4-actor-animation-coverage.tsv
exit /b %errorlevel%
