@echo off
REM Standalone single player, in a window. Flight, terrain, landing, walking and boarding all run
REM entirely client-side, so those need nothing else running.
REM
REM Resource deposits DO need the backend: run scripts\api.bat first, or the planet will simply
REM have no ore on it. The log says which happened -- look for "Placed N deposit(s)".
REM
REM Optional extras, appended to this line:
REM   -ShipStartX=38 -ShipStartY=0 -ShipStartZ=0    start ~1.7 km above the deposits, facing them
REM   -SpawnCharacter -CharacterDirZ=-1             also put a character on the far side
REM   -GatherCharacterId=5 -GatherStationId=1       who to credit when E is pressed at a deposit
REM
REM Gathering needs all three of: the API running, scripts\init-secrets.ps1 having been run once,
REM and a real character id. Which character belongs to which connection is not decided anywhere
REM yet -- the dedicated server has no login flow -- so it comes from the command line for now.
REM   -AutoDisembark                                step out the moment the ship lands
start "" "D:\Programming\UnrealEngineSource\Engine\Binaries\Win64\UnrealEditor.exe" "D:\Programming\SpaceMMO\client\SpaceMMO.uproject" -game -windowed -ResX=1600 -ResY=900 %*
