@echo off
chcp 65001 >nul
echo ======================================================
echo           ISLI PROGRAMLAMA DILI KURULUMU
echo ======================================================
echo.

echo [1/2] VS Code Eklentisi Yukleniyor...
where code >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    call code --install-extension "%~dp0isli-lang-1.1.0.vsix" --force
    if %ERRORLEVEL% NEQ 0 (
        for %%F in ("%~dp0*.vsix") do call code --install-extension "%%~fF" --force
    )
    echo VS Code eklentisi basariyla kuruldu!
) else (
    call code --install-extension "%~dp0isli-lang-1.1.0.vsix" --force
)

echo.
echo [2/2] isli.exe Sisteme Tanitiliyor...
set "TARGET_DIR=%LOCALAPPDATA%\Isli\bin"
if not exist "%TARGET_DIR%" mkdir "%TARGET_DIR%"
copy /Y "%~dp0isli.exe" "%TARGET_DIR%\isli.exe" >nul

echo %PATH% | find /I "%TARGET_DIR%" >nul
if %ERRORLEVEL% NEQ 0 (
    setx PATH "%PATH%;%TARGET_DIR%" >nul
    echo isli.exe PATH ortam degiskenine eklendi
) else (
    echo isli.exe zaten PATH'te tanimli.
)

echo.
echo ======================================================
echo Isli dili bilgisayariniza kuruldu.
echo Calismalarınızda basarilar.
echo ======================================================
pause
