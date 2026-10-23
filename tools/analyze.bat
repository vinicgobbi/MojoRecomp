@echo off
rem Legacy entry point. Forwards to tools\analyze.py.
py -3 "%~dp0analyze.py" %*
exit /b %ERRORLEVEL%
