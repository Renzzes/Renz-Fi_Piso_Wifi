@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM Renz-Fi — Customer Captive Portal export for MikroTik Hotspot upload
REM Workflow: portal/  →  build:mikrotik-portal  →  C:\Captive_Portal_BAT\
REM Does NOT flash ESP32, does NOT modify MikroTik, does NOT delete project trees.

cd /d "%~dp0.."
set "ROOT=%CD%"

echo ============================================
echo  Renz-Fi Captive Portal Export
echo ============================================
echo Project root: %ROOT%
echo.

if not exist "%ROOT%\portal\" (
  echo ERROR: portal\ not found. Canonical source missing.
  exit /b 1
)
if not exist "%ROOT%\package.json" (
  echo ERROR: package.json not found at project root.
  exit /b 1
)
if not exist "%ROOT%\scripts\build-mikrotik-portal.mjs" (
  echo ERROR: scripts\build-mikrotik-portal.mjs not found.
  exit /b 1
)
if not exist "%ROOT%\scripts\export-captive-portal.mjs" (
  echo ERROR: scripts\export-captive-portal.mjs not found.
  exit /b 1
)

where npm >nul 2>&1
if errorlevel 1 (
  echo ERROR: npm is not on PATH. Install Node.js or open a Developer shell.
  exit /b 1
)

set "RENZFI_APPLIANCE_BASE_URL=http://10.10.10.2"
set "RENZFI_CAPTIVE_EXPORT_DIR=C:\Captive_Portal_BAT"
set "RENZFI_CAPTIVE_EXPORT_HISTORY=C:\Captive_Portal_BAT_HISTORY"

echo Canonical source:  portal\
echo Build command:     RENZFI_APPLIANCE_BASE_URL=%RENZFI_APPLIANCE_BASE_URL% npm run build:mikrotik-portal
echo Export destination: %RENZFI_CAPTIVE_EXPORT_DIR%
echo.
echo [1/3] Building MikroTik portal from portal\ ...
echo.

call npm run build:mikrotik-portal
if errorlevel 1 (
  echo.
  echo ERROR: build:mikrotik-portal FAILED.
  echo Previous %RENZFI_CAPTIVE_EXPORT_DIR% was NOT replaced.
  exit /b 1
)

echo.
echo [2/3] Checking source/generated sync ...
call node "%ROOT%\scripts\check-captive-portal-source-sync.mjs"
if errorlevel 1 (
  echo.
  echo ERROR: source/generated sync check FAILED.
  echo Previous %RENZFI_CAPTIVE_EXPORT_DIR% was NOT replaced.
  exit /b 1
)

echo.
echo [3/3] Exporting production overlay to %RENZFI_CAPTIVE_EXPORT_DIR% ...
call node "%ROOT%\scripts\export-captive-portal.mjs"
if errorlevel 1 (
  echo.
  echo ERROR: export FAILED.
  echo Previous package may be incomplete — re-run after fixing the error.
  exit /b 1
)

echo.
echo ============================================
echo  SUCCESS
echo  Upload ALL overlay files in:
echo    %RENZFI_CAPTIVE_EXPORT_DIR%
echo  including status.html and renzfi-style.css
echo  into the MikroTik hotspot\ directory (overwrite matching names).
echo  Do not skip status.html — /status is a separate Hotspot servlet.
echo ============================================
exit /b 0
