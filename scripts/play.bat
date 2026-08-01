@echo off
REM Standalone single player, in a window. No server and no backend needed: flight, terrain,
REM landing, walking and boarding all run entirely client-side today.
REM
REM Optional extras, appended to this line:
REM   -ShipStartX=178 -ShipStartY=0 -ShipStartZ=0   start just above the planet instead of 200 km out
REM   -SpawnCharacter -CharacterDirZ=-1             also put a character on the far side
REM   -AutoDisembark                                step out the moment the ship lands
start "" "D:\Programming\UnrealEngineSource\Engine\Binaries\Win64\UnrealEditor.exe" "D:\Programming\SpaceMMO\client\SpaceMMO.uproject" -game -windowed -ResX=1600 -ResY=900 %*
