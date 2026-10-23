@echo off
rem Legacy entry point. Forwards to tools\recompile.py.
py -3 "%~dp0recompile.py" %*
exit /b %ERRORLEVEL%
