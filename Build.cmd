@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-EnchantingFreedomAE.ps1" %*
set EXITCODE=%ERRORLEVEL%
echo.
if not "%EXITCODE%"=="0" (
    echo Build failed with exit code %EXITCODE%.
) else (
    echo Build finished successfully.
)
pause
exit /b %EXITCODE%
