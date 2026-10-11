@echo off
setlocal
cd /d "%~dp0.."

echo BrickStore Brother QL-810W label service
echo =========================================

echo This window MUST stay open while printing from BrickStore.
echo BrickStore talks to http://127.0.0.1:8013
echo The Python service then talks to the printer at tcp://192.168.1.7
echo.

where py >nul 2>&1
if errorlevel 1 (
    echo ERROR: Python launcher ^(py.exe^) was not found.
    echo Install Python 3 with the Python launcher enabled.
    goto :fail
)

set "VENV=%~dp0.brother_ql_venv"
if not exist "%VENV%\Scripts\python.exe" (
    echo Creating local Python environment...
    py -3 -m venv "%VENV%"
    if errorlevel 1 goto :fail
)

"%VENV%\Scripts\python.exe" -c "import brother_ql, PIL" >nul 2>&1
if errorlevel 1 (
    echo Installing brother_ql and Pillow...
    "%VENV%\Scripts\python.exe" -m pip install --upgrade pip brother_ql Pillow
    if errorlevel 1 goto :fail
)

if "%BROTHER_QL_IP%"=="" (
    set /p BROTHER_QL_IP=Enter the QL-810W IP address [192.168.1.7]: 
)
if "%BROTHER_QL_IP%"=="" set "BROTHER_QL_IP=192.168.1.7"

echo.
echo Starting service...
echo HTTP service : http://127.0.0.1:8013
echo Printer      : tcp://%BROTHER_QL_IP%
echo Backend      : network
echo Model        : QL-810W
echo Label        : DK-11204 / 17x54
echo.
echo When you see "Brother QL service listening", test:
echo     http://127.0.0.1:8013/health
echo.

"%VENV%\Scripts\python.exe" "%~dp0brickstore_brother_ql_server.py" --printer "tcp://%BROTHER_QL_IP%" --backend network --model QL-810W --host 127.0.0.1 --port 8013 --log-level DEBUG
set "RC=%ERRORLEVEL%"

echo.
echo ============================================================
echo The Brother QL service STOPPED with exit code %RC%.
echo If BrickStore says "Connection refused", this service is not running.
echo Copy the error shown above if it stopped unexpectedly.
echo ============================================================
pause
exit /b %RC%

:fail
echo.
echo ============================================================
echo Could not start the Brother QL service.
echo Copy the error shown above.
echo ============================================================
pause
exit /b 1
