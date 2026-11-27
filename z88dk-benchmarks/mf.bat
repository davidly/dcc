@echo off
python "%~dp0bench.py" build %*
exit /b %errorlevel%
