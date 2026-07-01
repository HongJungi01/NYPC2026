@echo off
setlocal

REM Kill process on port 8770 if already running
for /f "tokens=5" %%a in ('netstat -ano 2^>nul ^| findstr /C:":8770 "') do (
    taskkill /f /pid %%a >nul 2>&1
)
timeout /t 1 /nobreak >nul

REM Launch viewer server from engine directory
pushd "%~dp0engine"
python viewer_server.py --open
if errorlevel 1 (
    python3 viewer_server.py --open
)
popd

pause
