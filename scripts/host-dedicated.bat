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
cd /d "D:\Programming\SpaceMMO\client\Saved\StagedBuilds\WindowsServer"
SpaceMMOServer.exe -log -port=7777
