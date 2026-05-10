@echo off
rem envy-managed _ENVY_PRODUCT_SCRIPT_VERSION=@@ENVY_PRODUCT_SCRIPT_VERSION@@
for /f "delims=" %%i in ('call "%~dp0envy.bat" product "@@PRODUCT_NAME@@"') do set "PRODUCT_PATH=%%i"
if not defined PRODUCT_PATH (
    echo envy: failed to resolve product '@@PRODUCT_NAME@@' 1>&2
    exit /b 1
)
call "%PRODUCT_PATH%" %*
exit /b %ERRORLEVEL%
