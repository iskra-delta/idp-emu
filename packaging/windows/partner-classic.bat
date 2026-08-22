@echo off
"%~dp0partner.exe" --model crt --system-floppy %*
exit /b %errorlevel%
