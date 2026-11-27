@echo off
python "%~dp0bench.py" run %*
exit /b %errorlevel%
