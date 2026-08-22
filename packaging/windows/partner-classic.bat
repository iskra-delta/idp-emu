@echo off
"%~dp0partner.exe" --model crt --system-floppy --boot floppy %*
exit /b %errorlevel%
