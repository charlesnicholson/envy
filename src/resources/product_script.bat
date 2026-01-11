@echo off
rem envy-managed @@ENVY_VERSION@@
for /f "delims=" %%i in ('call "%~dp0envy.exe" product "@@PRODUCT_NAME@@"') do set "PRODUCT_PATH=%%i"
if not defined PRODUCT_PATH (
    echo envy: failed to resolve product '@@PRODUCT_NAME@@' 1>&2
    exit /b 1
)
"%PRODUCT_PATH%" %*
