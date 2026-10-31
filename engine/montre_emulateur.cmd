@echo off
rem Double-clic : construit l'app montre et la lance sur la montre virtuelle Wear OS (voir tools\montre_emulateur.js)
node "%~dp0tools\montre_emulateur.js" %*
pause
