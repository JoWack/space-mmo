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
REM That is a stale server, not a network fault. A newly added component is worse, because it
REM produces no error at all -- the key simply does nothing, since the server has never heard of
REM the action it sends. That cost a session on the docking component.
REM
REM All of that was already written in this comment on the day it happened, and it did not help,
REM because a comment cannot run. check-staged-server.ps1 can, and this refuses to launch without
REM it -- including when the check itself fails, since a guard that cannot tell must not wave you
REM through.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0check-staged-server.ps1"

if errorlevel 1 (
    exit /b 1
)

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
