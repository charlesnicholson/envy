@echo off
rem envy-managed @@ENVY_VERSION@@
for /f "delims=" %%i in ('"%~dp0envy" product "@@PRODUCT_NAME@@"') do set "PRODUCT_PATH=%%i"
"%PRODUCT_PATH%" %*
