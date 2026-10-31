@echo off
rem Double-clic : construit l'app téléphone et la lance sur le téléphone virtuel Android (voir tools\montre_emulateur.js)
node "%~dp0tools\montre_emulateur.js" --telephone %*
pause
