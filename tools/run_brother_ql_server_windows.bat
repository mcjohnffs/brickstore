@echo off
setlocal
cd /d "%~dp0.."

echo BrickStore Brother QL-810W label service
echo =========================================

where py >nul 2>&1
if errorlevel 1 (
    echo Python launcher ^(py.exe^) was not found.
    echo Install Python 3 and enable the Python launcher, then run this file again.
    pause
    exit /b 1
)

set "VENV=%~dp0.brother_ql_venv"
if not exist "%VENV%\Scripts\python.exe" (
    echo Creating local Python environment...
    py -3 -m venv "%VENV%" || goto :fail
)

"%VENV%\Scripts\python.exe" -c "import brother_ql, PIL" >nul 2>&1
if errorlevel 1 (
    echo Installing brother_ql and Pillow...
    "%VENV%\Scripts\python.exe" -m pip install --upgrade pip brother_ql Pillow || goto :fail
)

if "%BROTHER_QL_IP%"=="" (
    set /p BROTHER_QL_IP=Enter the QL-810W IP address: 
)
if "%BROTHER_QL_IP%"=="" goto :fail

echo.
echo Starting service at http://127.0.0.1:8013
echo Printer: tcp://%BROTHER_QL_IP%:9100
echo Keep this window open while printing from BrickStore.
echo.

"%VENV%\Scripts\python.exe" "%~dp0brickstore_brother_ql_server.py" --printer "tcp://%BROTHER_QL_IP%:9100" --model QL-810W --host 127.0.0.1 --port 8013
exit /b %errorlevel%

:fail
echo.
echo Could not start the Brother QL service.
pause
exit /b 1
