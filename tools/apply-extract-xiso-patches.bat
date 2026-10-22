@echo off
rem Legacy entry point. Forwards to tools\setup_toolchain.py apply-patch.
py -3 "%~dp0setup_toolchain.py" apply-patch extract-xiso %*
exit /b %ERRORLEVEL%
