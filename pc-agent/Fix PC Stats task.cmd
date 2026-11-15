@echo off
rem Double-click installer for the 240-MP PC Stats agent.
rem Asks for administrator, then runs pc-stats-agent.ps1 -Install.
rem That registers the hidden logon and resume-from-sleep task. It does not
rem start a second kind of agent.
cd /d "%~dp0"
net session >nul 2>&1
if not "%errorlevel%"=="0" (
    echo Requesting administrator so the scheduled task can be registered...
    powershell -NoProfile -Command "Start-Process -Verb RunAs -FilePath '%~f0'"
    exit /b
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0pc-stats-agent.ps1" -Install
echo.
pause
