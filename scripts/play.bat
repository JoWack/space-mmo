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
REM   -CharacterId=10                               play a specific character on your account
REM
REM Gathering needs three things, each set up once:
REM   scripts\init-secrets.ps1    the game server's own credential
REM   scripts\api.bat             running, in another window
REM   scripts\init-player.ps1     an account, a character, and secrets\player-login.txt
REM
REM Credentials are read from secrets\player-login.txt, NOT from this command line. Command lines
REM here mangle values -- -BackendEmail=someone@gmail.com has arrived as "someone@gmail .com", and
REM parsing stops at the space, producing a 401 that looks exactly like a wrong password.
REM
REM Without credentials the connection has no character, and gathering says so rather than
REM crediting somebody arbitrary. The character id is only a claim: the server checks it against
REM your session token and refuses any character your account does not own. Omit it and the client
REM plays the first character on the account.
start "" "D:\Programming\UnrealEngineSource\Engine\Binaries\Win64\UnrealEditor.exe" "D:\Programming\SpaceMMO\client\SpaceMMO.uproject" -game -windowed -ResX=1600 -ResY=900 %*
