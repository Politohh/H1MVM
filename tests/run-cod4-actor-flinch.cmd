@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /O2 /I src\client tests\cod4_actor_flinch.cpp /Fe:build\cod4_actor_flinch.exe /Fo:build\cod4_actor_flinch.obj
if errorlevel 1 exit /b 1
build\cod4_actor_flinch.exe "tests\fixtures\mwr-native-actor-codecs.tsv" "F:\CLIPS\Polito Best Clips All Games\TDM_PROMOD_STRIKE_5k_r700_SICK_NOSC_SKY.dm_1" "C:\Games\cod4_game_files\Mods\promodlive204\demos\dEbilzuuR_2k_deag_r700_crash.dm_1" "C:\Games\cod4_game_files\main\demos\city_cod4_to_h1_test_player_____.dm_1"
exit /b %errorlevel%
