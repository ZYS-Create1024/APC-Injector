:: SPDX-License-Identifier: MIT
:: Copyright (c) 2026 APC-Injector (GitHub: @ZYS-Create1024)

@echo off
rem Build script , == powershell -File build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
::If you can not run this script, please check your execution policy settings. You can change it by running "Set-ExecutionPolicy RemoteSigned" in an elevated PowerShell prompt.
exit /b %errorlevel%
