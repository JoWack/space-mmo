@echo off
REM The backend, on http://localhost:5080. Only needed for account and character work --
REM nothing in gameplay calls it yet.
REM
REM First time only, to create the schema and load content:
REM   dotnet run --project services\SpaceMMO.Api -- --seed
cd /d "D:\Programming\SpaceMMO"
set ASPNETCORE_ENVIRONMENT=Development
set ASPNETCORE_URLS=http://localhost:5080
REM The API's log as a file beside the game's (task 198), relative to services\SpaceMMO.Api.
set Logging__File__Path=..\..\client\Saved\Logs\Api.log
dotnet run --project services\SpaceMMO.Api --no-launch-profile
