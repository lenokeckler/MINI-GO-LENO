@echo off
setlocal enableextensions
title MiniGo Compiler - Instalacion automatica
color 0B

echo ============================================================
echo    MiniGo Compiler - Instalacion y compilacion automatica
echo ============================================================
echo.
echo  Este script instala TODO lo necesario y compila el proyecto.
echo  Requiere conexion a internet (descarga ~1-2 GB la primera vez).
echo.
pause

REM Trabajar siempre desde la carpeta del proyecto (donde esta este .bat)
cd /d "%~dp0"

set "MSYS=C:\msys64"
REM Entorno UCRT64 y conservar el directorio actual al entrar a bash
set "MSYSTEM=UCRT64"
set "CHERE_INVOKING=1"

REM ------------------------------------------------------------
REM  1. MSYS2 (toolchain base)
REM ------------------------------------------------------------
echo.
if exist "%MSYS%\usr\bin\bash.exe" (
    echo [OK]  MSYS2 ya esta instalado en %MSYS%
) else (
    echo [..]  MSYS2 no encontrado. Instalando con winget...
    winget install -e --id MSYS2.MSYS2 --accept-package-agreements --accept-source-agreements
    if not exist "%MSYS%\usr\bin\bash.exe" (
        echo.
        echo [ERROR] No se pudo instalar MSYS2 automaticamente.
        echo         Instalalo a mano desde https://www.msys2.org/  hacia  C:\msys64
        echo         y volve a ejecutar este script.
        echo.
        pause
        exit /b 1
    )
    echo [OK]  MSYS2 instalado.
)

REM ------------------------------------------------------------
REM  2. Dependencias MSYS2 (compilador, CMake, Ninja, ANTLR, LLVM, Qt6)
REM ------------------------------------------------------------
echo.
echo [..]  Instalando dependencias MSYS2 (puede tardar varios minutos)...
echo.
"%MSYS%\usr\bin\bash.exe" -lc "pacman -Sy --noconfirm && pacman -S --needed --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-antlr4-runtime-cpp mingw-w64-ucrt-x86_64-llvm mingw-w64-ucrt-x86_64-qt6-base"
if errorlevel 1 (
    echo.
    echo [ERROR] Fallo la instalacion de dependencias. Revisa tu conexion y reintenta.
    echo.
    pause
    exit /b 1
)
echo [OK]  Dependencias MSYS2 instaladas.

REM ------------------------------------------------------------
REM  2b. Java (JDK) - necesario para generar el parser con el JAR de ANTLR4.
REM      MSYS2 no empaqueta un JDK, asi que se instala con winget.
REM      CMake busca java en JAVA_HOME y en C:\Program Files\Java\...
REM ------------------------------------------------------------
echo.
REM ¿Ya hay un java utilizable? Si si, no instalamos nada.
where java >nul 2>nul
if not errorlevel 1 (
    echo [OK]  Java ya esta disponible en el PATH.
    goto :java_verificar
)

echo [..]  Java no encontrado. Instalando Microsoft OpenJDK 21 con winget...
winget install -e --id Microsoft.OpenJDK.21 --accept-package-agreements --accept-source-agreements

REM Refrescar JAVA_HOME desde el registro (winget lo deja a nivel de sistema,
REM pero esta ventana de cmd aun no lo ve).
for /f "tokens=2,*" %%a in ('reg query "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v JAVA_HOME 2^>nul') do set "JAVA_HOME=%%b"
if defined JAVA_HOME set "PATH=%JAVA_HOME%\bin;%PATH%"

REM Si aun no aparece, agregar al PATH las rutas tipicas donde quedan los JDK.
where java >nul 2>nul
if not errorlevel 1 goto :java_verificar
for /d %%d in ("C:\Program Files\Microsoft\jdk-*")         do set "PATH=%%d\bin;%PATH%"
for /d %%d in ("C:\Program Files\Eclipse Adoptium\jdk-*")  do set "PATH=%%d\bin;%PATH%"
for /d %%d in ("C:\Program Files\Java\jdk-*")              do set "PATH=%%d\bin;%PATH%"

:java_verificar
REM Verificacion final: java DEBE responder, o el build fallara al generar el parser.
echo.
echo [..]  Verificando que Java funcione...
java -version >nul 2>nul
if errorlevel 1 (
    echo.
    echo ============================================================
    echo [ERROR] Java se instalo pero ESTA ventana todavia no lo reconoce.
    echo         Es normal la primera vez ^(Windows no refresca el PATH^).
    echo.
    echo         SOLUCION ^(toma 5 segundos^):
    echo            1^) Cerra esta ventana.
    echo            2^) Volve a hacer doble clic en setup_windows.bat
    echo         La segunda corrida ya tomara el Java y compilara sin problemas.
    echo ============================================================
    echo.
    pause
    exit /b 1
)
echo [OK]  Java funciona correctamente:
java -version

REM ------------------------------------------------------------
REM  3. Compilar el proyecto (bash conserva esta carpeta por CHERE_INVOKING)
REM ------------------------------------------------------------
echo.
echo [..]  Compilando el proyecto MiniGo...
echo.
REM Borrar cualquier build/ previo (p.ej. si vino dentro del ZIP). Asi se
REM evita el conflicto de rutas absolutas y se compila siempre desde cero.
if exist "build" (
    echo [..]  Limpiando build/ anterior...
    rmdir /s /q "build"
)
"%MSYS%\usr\bin\bash.exe" -lc "cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build"
if errorlevel 1 (
    echo.
    echo [ERROR] Fallo la compilacion.
    echo.
    pause
    exit /b 1
)

REM ------------------------------------------------------------
REM  4. Abrir el editor
REM ------------------------------------------------------------
echo.
echo ============================================================
echo    LISTO! Compilacion exitosa. Abriendo el editor MiniGo...
echo ============================================================
echo.
echo  Para volver a abrir el editor mas tarde: doble clic en run_editor.bat
echo.
set "PATH=%MSYS%\ucrt64\bin;%PATH%"
start "" "%~dp0build\minigo_editor.exe"
pause
