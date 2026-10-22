@echo off
setlocal EnableExtensions DisableDelayedExpansion

rem Builds the COT runtime release candidate. The runtime is built from the
rem user's own game files, so it is produced locally instead of in CI; the
rem launcher release is built and published by .github/workflows/release.yml.

cd /d "%~dp0"
if errorlevel 1 exit /b 1

where node.exe >nul 2>nul
if errorlevel 1 (
  echo ERROR: Node.js is required to build a release package.
  exit /b 1
)

where tar.exe >nul 2>nul
if errorlevel 1 (
  echo ERROR: Windows tar.exe is required to create the runtime and source archives.
  exit /b 1
)

set "MOJORECOMP_RELEASE_VERSION="
for /f "tokens=2 delims==" %%V in ('findstr /b /c:"cot_runtime = " version.toml') do (
  for /f "tokens=* delims= " %%W in ("%%V") do set "MOJORECOMP_RELEASE_VERSION=%%~W"
)

if not defined MOJORECOMP_RELEASE_VERSION (
  echo ERROR: Could not read cot_runtime version from version.toml.
  exit /b 1
)

set "MOJORECOMP_RELEASE_TAG=cot-runtime-v%MOJORECOMP_RELEASE_VERSION%"
set "MOJORECOMP_RELEASE_REPOSITORY=https://github.com/vinicgobbi/MojoRecomp"

if not defined MOJORECOMP_UPDATE_BASE_URL (
  set "MOJORECOMP_UPDATE_BASE_URL=%MOJORECOMP_RELEASE_REPOSITORY%/releases/download/%MOJORECOMP_RELEASE_TAG%"
)
if not defined MOJORECOMP_UPDATE_NOTES_URL (
  set "MOJORECOMP_UPDATE_NOTES_URL=%MOJORECOMP_RELEASE_REPOSITORY%/releases/tag/%MOJORECOMP_RELEASE_TAG%"
)

echo Preparing COT runtime release %MOJORECOMP_RELEASE_VERSION% ^(%MOJORECOMP_RELEASE_TAG%^)...
echo Update assets: %MOJORECOMP_UPDATE_BASE_URL%
echo Release notes: %MOJORECOMP_UPDATE_NOTES_URL%
echo.

if /i "%~1"=="--show-config" exit /b 0

call build-smoke.bat
if errorlevel 1 exit /b 1

call npm.cmd --prefix launcher run package:runtime-release
if errorlevel 1 exit /b 1

echo.
echo Runtime release candidate created under .release\
echo Publish its files as GitHub release %MOJORECOMP_RELEASE_TAG%.
exit /b 0
