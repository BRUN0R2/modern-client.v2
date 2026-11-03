@echo off
rem SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
rem SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

setlocal
cd /d "%~dp0"

set "releasePreset=release-windows-msvc"
set "developerPreset=dev-windows-msvc"
set "gameArguments=+map de_dust2"

:menu
cls
echo.
echo Modern-Client.v2
echo ================
echo.
echo 1. Build and open the game (Release)
echo 2. Build and open the game (Developer)
echo 3. Clean build (delete the whole build folder)
echo 4. Exit
echo.
choice /c 1234 /n /m "Select an option: "
if errorlevel 4 exit /b 0
if errorlevel 3 goto clean
if errorlevel 2 set "preset=%developerPreset%" & goto buildAndOpen
if errorlevel 1 set "preset=%releasePreset%" & goto buildAndOpen
goto menu

:buildAndOpen
echo.
powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -Command ". .\scripts\devShell.ps1; cmake --preset %preset%; if ($LASTEXITCODE -eq 0) { cmake --build --preset %preset% }; exit $LASTEXITCODE"
if errorlevel 1 goto failed
"build\%preset%\bin\next.exe" %gameArguments%
if errorlevel 1 goto failed
goto menu

:clean
if not exist build goto menu
echo.
echo Everything in the build folder will be deleted:
dir /b build
echo.
choice /c NY /n /m "Delete it? [Y/N] "
if not errorlevel 2 goto menu
echo Deleting build
rd /s /q build
if exist build goto failed
goto menu

:failed
echo.
echo Failed. See the messages above.
pause
goto menu
