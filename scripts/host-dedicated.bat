@echo off
REM The cooked dedicated server, listening on 7777.
REM
REM Rebuild it after any code change with:
REM   cd /d D:\Programming\UnrealEngineSource
REM   Engine\Build\BatchFiles\RunUAT.bat BuildCookRun -project="D:\Programming\SpaceMMO\client\SpaceMMO.uproject" -noP4 -utf8output -platform=Win64 -serverconfig=Development -server -noclient -build -cook -stage -pak
cd /d "D:\Programming\SpaceMMO\client\Saved\StagedBuilds\WindowsServer"
SpaceMMOServer.exe -log -port=7777
