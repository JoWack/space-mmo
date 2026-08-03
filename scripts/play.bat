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
REM   -AutoDisembark                                step out the moment the ship lands
REM   -BackendEmail=you@example.com -BackendPassword=... -CharacterId=8
REM                                                 sign in, so the server knows who you are
REM
REM Gathering needs all three of: the API running, scripts\init-secrets.ps1 having been run once,
REM and the credentials above. Without them the connection has no character, and gathering says so
REM rather than crediting somebody arbitrary.
REM
REM The character id is only a claim. The server checks it against your session token and refuses
REM any character your account does not own. Omit -CharacterId= to play the first one you have.
start "" "D:\Programming\UnrealEngineSource\Engine\Binaries\Win64\UnrealEditor.exe" "D:\Programming\SpaceMMO\client\SpaceMMO.uproject" -game -windowed -ResX=1600 -ResY=900 %*
