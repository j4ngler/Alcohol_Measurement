@echo off
REM Script để build và flash code ESP-IDF
REM Tự động build, flash và mở monitor

echo ========================================
echo ESP-IDF Build and Flash Script
echo ========================================
echo.

REM Kiểm tra ESP-IDF đã được setup chưa
where idf.py >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: ESP-IDF chua duoc setup!
    echo Vui long mo ESP-IDF Command Prompt hoac chay export script
    pause
    exit /b 1
)

echo [1/4] Building project...
idf.py build
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Build failed!
    pause
    exit /b 1
)

echo.
echo [2/4] Flashing to ESP32...
idf.py flash
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Flash failed!
    echo Kiem tra ESP32 da ket noi chua
    pause
    exit /b 1
)

echo.
echo [3/4] Opening monitor...
echo.
echo ========================================
echo Press Ctrl+] to exit monitor
echo ========================================
echo.

idf.py monitor

pause

