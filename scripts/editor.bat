@echo off
REM Opens the project in the editor for interactive work.
REM
REM Must be the SOURCE engine. Once BuildCookRun has run, the project's binaries are compiled
REM against D:\Programming\UnrealEngineSource and the launcher engine can no longer load them --
REM it fails with "The game module 'SpaceMMOCore' could not be found", which is misleading.
start "" "D:\Programming\UnrealEngineSource\Engine\Binaries\Win64\UnrealEditor.exe" "D:\Programming\SpaceMMO\client\SpaceMMO.uproject"
