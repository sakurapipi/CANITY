@echo off
chcp 65001 >nul
setlocal
echo.
echo   Removing the "downloaded from the Internet" mark (Zone.Identifier) ...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "Get-ChildItem -LiteralPath '%~dp0' -Filter *.exe -Recurse | Unblock-File"
echo.
echo   Done. Now you can double-click canity.exe without the warning.
echo.
pause
