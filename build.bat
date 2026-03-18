@echo off
setlocal enabledelayedexpansion

REM MAGDA DAW - Windows Build Script
REM Usage: build.bat [command]
REM Commands: debug, release, test, run, run-console, clean, configure, help

REM --- Auto-detect MSVC environment ---
if "%INCLUDE%"=="" (
    echo Setting up MSVC environment...

    REM Find MSVC version
    for /f "delims=" %%v in ('dir /b "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC" 2^>nul') do set "MSVC_VER=%%v"
    if not defined MSVC_VER (
        echo Error: Could not find MSVC. Install Visual Studio with C++ workload.
        exit /b 1
    )

    REM Find Windows SDK version
    for /f "delims=" %%v in ('dir /b "C:\Program Files (x86)\Windows Kits\10\Include" 2^>nul') do set "WINSDK_VER=%%v"
    if not defined WINSDK_VER (
        echo Error: Could not find Windows SDK.
        exit /b 1
    )

    set "VS_ROOT=C:\Program Files\Microsoft Visual Studio\18\Community"
    set "WINSDK_ROOT=C:\Program Files (x86)\Windows Kits\10"

    set "INCLUDE=!VS_ROOT!\VC\Tools\MSVC\!MSVC_VER!\include;!WINSDK_ROOT!\Include\!WINSDK_VER!\ucrt;!WINSDK_ROOT!\Include\!WINSDK_VER!\shared;!WINSDK_ROOT!\Include\!WINSDK_VER!\um;!WINSDK_ROOT!\Include\!WINSDK_VER!\winrt;!WINSDK_ROOT!\Include\!WINSDK_VER!\cppwinrt"
    set "LIB=!VS_ROOT!\VC\Tools\MSVC\!MSVC_VER!\lib\x64;!WINSDK_ROOT!\Lib\!WINSDK_VER!\ucrt\x64;!WINSDK_ROOT!\Lib\!WINSDK_VER!\um\x64"
    set "PATH=!VS_ROOT!\VC\Tools\MSVC\!MSVC_VER!\bin\Hostx64\x64;!WINSDK_ROOT!\bin\!WINSDK_VER!\x64;!PATH!"

    echo   MSVC %MSVC_VER%, SDK %WINSDK_VER%
)

REM --- Build directories ---
set "BUILD_DIR=cmake-build-debug"
set "BUILD_DIR_RELEASE=cmake-build-release"
set "MAGDA_EXE=%BUILD_DIR%\magda\daw\magda_daw_app_artefacts\Debug\MAGDA.exe"
set "MAGDA_EXE_RELEASE=%BUILD_DIR_RELEASE%\magda\daw\magda_daw_app_artefacts\Release\MAGDA.exe"
set "TEST_EXE=%BUILD_DIR%\tests\magda_tests.exe"

REM --- Parse command ---
if "%1"=="" goto :debug
if "%1"=="debug" goto :debug
if "%1"=="release" goto :release
if "%1"=="configure" goto :configure
if "%1"=="test" goto :test
if "%1"=="test-build" goto :test-build
if "%1"=="test-verbose" goto :test-verbose
if "%1"=="run" goto :run
if "%1"=="run-console" goto :run-console
if "%1"=="run-release" goto :run-release
if "%1"=="clean" goto :clean
if "%1"=="help" goto :help
echo Unknown command: %1
goto :help

:debug
echo Building MAGDA DAW (Debug)...
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo Configuring project...
    cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMAGDA_BUILD_TESTS=ON
)
cmake --build "%BUILD_DIR%"
goto :end

:release
echo Building MAGDA DAW (Release)...
if not exist "%BUILD_DIR_RELEASE%" mkdir "%BUILD_DIR_RELEASE%"
cmake -S . -B "%BUILD_DIR_RELEASE%" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "%BUILD_DIR_RELEASE%"
goto :end

:configure
echo Reconfiguring MAGDA DAW (Debug)...
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMAGDA_BUILD_TESTS=ON
goto :end

:test-build
echo Building tests...
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo Configuring project with tests enabled...
    cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMAGDA_BUILD_TESTS=ON
)
cmake --build "%BUILD_DIR%" --target magda_tests
goto :end

:test
call :test-build
if errorlevel 1 goto :end
echo Running all tests...
"%TEST_EXE%"
goto :end

:test-verbose
call :test-build
if errorlevel 1 goto :end
echo Running tests (verbose)...
"%TEST_EXE%" -s
goto :end

:run
call :debug
if errorlevel 1 goto :end
echo Running MAGDA DAW...
start "" "%MAGDA_EXE%"
goto :end

:run-console
call :debug
if errorlevel 1 goto :end
echo Running MAGDA DAW (console mode)...
"%MAGDA_EXE%"
goto :end

:run-release
call :release
if errorlevel 1 goto :end
echo Running MAGDA DAW (Release)...
start "" "%MAGDA_EXE_RELEASE%"
goto :end

:clean
echo Cleaning build artifacts...
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
if exist "%BUILD_DIR_RELEASE%" rmdir /s /q "%BUILD_DIR_RELEASE%"
goto :end

:help
echo.
echo MAGDA DAW - Windows Build Script
echo.
echo Usage: build.bat [command]
echo.
echo Build commands:
echo   debug       - Build debug version (default)
echo   release     - Build release version
echo   configure   - Reconfigure CMake
echo   clean       - Remove build artifacts
echo.
echo Run commands:
echo   run         - Build and launch the application
echo   run-console - Build and run with console output
echo   run-release - Build and run release version
echo.
echo Test commands:
echo   test        - Build and run all tests
echo   test-build  - Build tests only
echo   test-verbose - Run tests with verbose output
echo.
goto :end

:end
