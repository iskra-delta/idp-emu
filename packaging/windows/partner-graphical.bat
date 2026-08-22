@echo off
"%~dp0partner.exe" --model gdp --system-hdd %*
exit /b %errorlevel%
