@echo off
setlocal
if defined PYTHON goto custom_python
where python >nul 2>nul
if not errorlevel 1 goto python_on_path
where py >nul 2>nul
if errorlevel 1 goto missing_python
py -3 "%~dp0scripts\ma.py" %*
exit /b %errorlevel%
:python_on_path
python "%~dp0scripts\ma.py" %*
exit /b %errorlevel%
:custom_python
"%PYTHON%" "%~dp0scripts\ma.py" %*
exit /b %errorlevel%
:missing_python
echo Python 3.7 or newer is required. Install Python or set PYTHON to its executable. 1>&2
exit /b 1
