@echo off
setlocal EnableExtensions

pushd "%~dp0"
if errorlevel 1 (
    echo Could not open the extracted developer kit folder.
    pause
    exit /b 1
)

if not exist "tools\developer_studio.py" (
    echo Workbench files are missing. Extract the entire ZIP before starting it.
    pause
    popd
    exit /b 1
)

set "PYTHON_CMD="
py -3 -c "import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)" >nul 2>&1
if not errorlevel 1 set "PYTHON_CMD=py -3"

if not defined PYTHON_CMD (
    python -c "import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)" >nul 2>&1
    if not errorlevel 1 set "PYTHON_CMD=python"
)

if not defined PYTHON_CMD (
    echo Python 3.10 or newer was not found.
    echo Install Python, enable it in PATH, and double-click this file again.
    echo See README.md for the build tools required after the workbench opens.
    pause
    popd
    exit /b 1
)

echo Starting ESP Mini App Developer Studio from:
echo "%CD%"
echo Keep this window open while using the workbench. Press Ctrl+C to stop.
%PYTHON_CMD% "tools\developer_studio.py" %*
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" (
    echo.
    echo Workbench stopped with an error. See the message above.
    pause
)
popd
exit /b %RESULT%
