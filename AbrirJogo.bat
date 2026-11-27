@echo off
rem SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
rem SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

setlocal
pushd "%~dp0" || exit /b 1

rem O Release, otimizado, quando compilado; senao o Debug, muito mais lento com bots e jogadores.
set "game=build\release-windows-msvc\bin\next.exe"
if not exist "%game%" (
    set "game=build\dev-windows-msvc\bin\next.exe"
    echo Aviso: abrindo o build Debug, lento com bots. Compile o Release: buildMenu.cmd, opcao 1.
)
if not exist "%game%" goto missing

"%game%" %*
set "gameExit=%errorlevel%"
if not "%gameExit%"=="0" (
    echo.
    echo O jogo encerrou com o codigo %gameExit%.
    pause
)
popd
exit /b %gameExit%

:missing
echo Jogo compilado nao encontrado em "%game%".
echo Use buildMenu.cmd e selecione a opcao 1 (Release) para compilar.
pause
popd
exit /b 1
