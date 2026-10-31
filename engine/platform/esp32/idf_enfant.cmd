@echo off
rem Firmware de l'edition enfant : les reglages de sdkconfig (recopies a chaque fois : broches, ecran...) plus
rem CONFIG_ECHOES_ENFANT=y, dans build_enfant ; flashe engine\data\enfant\monde_esp32.pak (maj_monde.js --enfant)
rem   idf_enfant.cmd build    ou    idf_enfant.cmd -p COM11 flash monitor
cd /d %~dp0
if not exist build_enfant mkdir build_enfant
powershell -NoProfile -Command "$s = [IO.File]::ReadAllText('sdkconfig') -replace '(?m)^(# )?CONFIG_ECHOES_ENFANT( is not set|=.*)\r?\n', ''; [IO.File]::WriteAllText('build_enfant\sdkconfig', $s.TrimEnd() + [Environment]::NewLine + 'CONFIG_ECHOES_ENFANT=y' + [Environment]::NewLine)"
if errorlevel 1 exit /b 1
call "%~dp0idf.cmd" -B build_enfant -D SDKCONFIG=build_enfant/sdkconfig %*
