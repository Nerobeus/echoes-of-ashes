@echo off
rem Lance idf.py avec l'ESP-IDF 5.2.3 du projet tiny386 (outils dans son dossier .espressif)
rem   idf.cmd build | idf.cmd -p COM8 flash (chargeur, table, otadata, montre) | idf.cmd menuconfig
set IDF_TOOLS_PATH=D:\Documents\Tuxdroid\esp32-dos-project\.espressif
call D:\Documents\Tuxdroid\esp32-dos-project\esp-idf\export.bat >nul
cd /d %~dp0
idf.py %*
