@echo off
REM Connects a client to a dedicated server on this machine. Run it twice for two players.
REM Give each one its own log so they do not overwrite each other.
if "%~1"=="" (set LOGNAME=ClientA.log) else (set LOGNAME=%~1)
start "" "D:\Programming\UnrealEngineSource\Engine\Binaries\Win64\UnrealEditor.exe" "D:\Programming\SpaceMMO\client\SpaceMMO.uproject" 127.0.0.1:7777 -game -windowed -ResX=1280 -ResY=720 -log=%LOGNAME%
