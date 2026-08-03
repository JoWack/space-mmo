@echo off
REM The cooked dedicated server, listening on 7777.
REM
REM  *** RE-COOK AFTER EVERY CODE CHANGE. ***
REM
REM Clients are built from current source; this server is a snapshot from whenever it was last
REM cooked. If a replicated property has been added or removed since, the two disagree about the
REM replication layout and the client log fills with:
REM
REM   ReceivedBunch: FieldCache == nullptr
REM   ReadFieldHeaderAndPayload: GetFromIndex failed
REM
REM That is a stale server, not a network fault.
REM
REM Rebuild it after any code change with:
REM   cd /d D:\Programming\UnrealEngineSource
REM   Engine\Build\BatchFiles\RunUAT.bat BuildCookRun -project="D:\Programming\SpaceMMO\client\SpaceMMO.uproject" -noP4 -utf8output -platform=Win64 -serverconfig=Development -server -noclient -build -cook -stage -pak
REM The staged server lives under Saved\StagedBuilds, so its idea of "the project directory" is in
REM there too, and the default relative path to secrets\ finds nothing. Without the credential it
REM cannot verify anyone's identity, and every player is refused their own character -- which reads
REM as an authentication bug rather than a missing file. Pointed at the repository copy explicitly.
set SPACEMMO_SERVICE_SECRET_FILE=D:\Programming\SpaceMMO\secrets\service-secret.txt

REM Launched by full path rather than by cd'ing in. A shell whose working directory is the staging
REM folder holds a lock on it even after the server itself has exited, and the next cook then fails
REM at the very end with "Failed to delete staging directory" -- twenty-five minutes of build and
REM cook thrown away, with nothing in the message pointing at a leftover window.
"D:\Programming\SpaceMMO\client\Saved\StagedBuilds\WindowsServer\SpaceMMOServer.exe" -log -port=7777
