@echo off
rem Compila e abre a versao grafica do NEXUS (requer GCC/MinGW no PATH)
cd /d "%~dp0"
gcc -std=c99 -Wall -Wextra src/gui.c src/jogo.c src/mapa.c src/missoes.c -o nexus_gui.exe -mwindows -lgdi32 -lmsimg32 -lm
if errorlevel 1 (
    echo.
    echo Falha ao compilar. Verifique se o GCC ^(MinGW^) esta instalado.
    pause
    exit /b 1
)
start "" nexus_gui.exe
