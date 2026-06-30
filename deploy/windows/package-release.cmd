@echo off
setlocal

set "QT_DIR=C:\Qt\Qt\6.11.1\mingw_64"
set "CMAKE=C:\Qt\Qt\Tools\CMake_64\bin\cmake.exe"
set "INNO_SETUP=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
set "MINGW_BIN=C:\Qt\Qt\Tools\mingw1310_64\bin"

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..\..") do set "ROOT=%%~fI"
set "BUILD_DIR=%ROOT%\build\Desktop_Qt_6_11_1_MinGW_64_bit-Release"
set "DIST_DIR=%ROOT%\deploy\windows\dist\BlinqMessenger"
set "INSTALLER_DIR=%ROOT%\deploy\windows\installer"
set "QT_BIN=%QT_DIR%\bin"
set "WINDEPLOYQT=%QT_BIN%\windeployqt.exe"
set "QTPATHS=%QT_BIN%\qtpaths.exe"

if not exist "%CMAKE%" (
    echo CMake was not found at "%CMAKE%".
    exit /b 1
)
if not exist "%WINDEPLOYQT%" (
    echo windeployqt was not found at "%WINDEPLOYQT%".
    exit /b 1
)
if not exist "%QTPATHS%" (
    echo qtpaths was not found at "%QTPATHS%".
    exit /b 1
)

set "PATH=%QT_BIN%;%MINGW_BIN%;%PATH%"

"%CMAKE%" -S "%ROOT%" -B "%BUILD_DIR%" -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_DIR%"
if errorlevel 1 exit /b %ERRORLEVEL%

"%CMAKE%" --build "%BUILD_DIR%" --config Release
if errorlevel 1 exit /b %ERRORLEVEL%

if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"
mkdir "%DIST_DIR%"
if errorlevel 1 exit /b %ERRORLEVEL%
if not exist "%INSTALLER_DIR%" mkdir "%INSTALLER_DIR%"
if errorlevel 1 exit /b %ERRORLEVEL%

copy /y "%BUILD_DIR%\messenger.exe" "%DIST_DIR%\messenger.exe" >nul
if errorlevel 1 exit /b %ERRORLEVEL%
copy /y "%ROOT%\help.html" "%DIST_DIR%\help.html" >nul
if errorlevel 1 exit /b %ERRORLEVEL%
mkdir "%DIST_DIR%\assets" 2>nul
copy /y "%ROOT%\assets\appicon.ico" "%DIST_DIR%\assets\appicon.ico" >nul
if errorlevel 1 exit /b %ERRORLEVEL%

pushd "%ROOT%"
"%WINDEPLOYQT%" --qtpaths "%QTPATHS%" --release --no-translations --compiler-runtime "deploy\windows\dist\BlinqMessenger\messenger.exe"
set "DEPLOY_EXIT=%ERRORLEVEL%"
popd
if not "%DEPLOY_EXIT%"=="0" exit /b %DEPLOY_EXIT%

for %%D in (Qt6Core.dll Qt6Gui.dll Qt6Network.dll Qt6Multimedia.dll) do (
    if not exist "%DIST_DIR%\%%D" (
        echo Release package is missing %%D after windeployqt.
        exit /b 1
    )
)

if /i "%~1"=="--skip-installer" goto done

if not exist "%INNO_SETUP%" (
    echo Inno Setup compiler was not found at "%INNO_SETUP%".
    exit /b 1
)

"%INNO_SETUP%" "%SCRIPT_DIR%BlinqMessenger.iss" "/DSourceDir=%DIST_DIR%" "/DOutputDir=%INSTALLER_DIR%"
if errorlevel 1 exit /b %ERRORLEVEL%

:done
echo Release package ready: %DIST_DIR%
if /i not "%~1"=="--skip-installer" echo Installer output: %INSTALLER_DIR%
exit /b 0
