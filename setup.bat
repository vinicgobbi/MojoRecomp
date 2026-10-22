@echo off
rem Legacy entry point. The setup logic lives in tools\setup_toolchain.py.
py -3 "%~dp0tools\setup_toolchain.py" %*
exit /b %ERRORLEVEL%
