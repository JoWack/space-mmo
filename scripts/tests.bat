@echo off
rem Runs the client automation suite. See tests.ps1 for why it does not use -testexit.
rem
rem   scripts\tests.bat                     run everything
rem   scripts\tests.bat SpaceMMO.HUD        run one category
rem   scripts\tests.bat SpaceMMO 176        run everything and assert the count
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tests.ps1" -Filter "%~1" -Expected "%~2"
exit /b %ERRORLEVEL%
