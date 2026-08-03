@echo off
REM Connects a client to a dedicated server on this machine.
REM
REM   scripts\join.bat a      player A: secrets\player-a.txt, logging to ClientA.log
REM   scripts\join.bat b      player B: secrets\player-b.txt, logging to ClientB.log
REM   scripts\join.bat        the default login, logging to Client.log
REM
REM Anything after the letter is passed through to the client, so extra flags still work.
REM
REM The letter form exists because the previous version took a log name and silently discarded
REM every argument after it. -BackendLoginFile= never arrived, both clients fell back to the same
REM login file, and both joined as the SAME character -- the server reported "Connection
REM identified as character 10" twice, which is easy to read past when you are expecting two
REM different numbers.
setlocal

set EXTRA=

if /I "%~1"=="a" (
    set LOGNAME=ClientA.log
    set LOGIN=-BackendLoginFile=D:\Programming\SpaceMMO\secrets\player-a.txt
    shift
) else if /I "%~1"=="b" (
    set LOGNAME=ClientB.log
    set LOGIN=-BackendLoginFile=D:\Programming\SpaceMMO\secrets\player-b.txt
    shift
) else (
    set LOGNAME=Client.log
    set LOGIN=
)

REM Collected one argument at a time: %* ignores shift, so it cannot be used after one.
:collect
if "%~1"=="" goto run
set EXTRA=%EXTRA% %1
shift
goto collect

:run
start "" "D:\Programming\UnrealEngineSource\Engine\Binaries\Win64\UnrealEditor.exe" "D:\Programming\SpaceMMO\client\SpaceMMO.uproject" 127.0.0.1:7777 -game -windowed -ResX=1280 -ResY=720 -log=%LOGNAME% %LOGIN%%EXTRA%
