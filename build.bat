@echo off
setlocal enabledelayedexpansion

echo ===================================================
echo  Building preAlphaVoiceChat with Clang...
echo ===================================================

clang main.c -o voicechat.exe ^
    -lws2_32 -lbcrypt -lavrt -lole32 -luser32 -lgdi32 -lcomctl32 -loleaut32 ^
    -Xlinker /subsystem:windows ^
    -Wall -Wextra -Wno-unused-parameter -Wno-unused-function

if %ERRORLEVEL% equ 0 (
    echo.
    echo ===================================================
    echo  [SUCCESS] voicechat.exe built successfully!
    echo ===================================================
) else (
    echo.
    echo ===================================================
    echo  [ERROR] Compilation or linking failed.
    echo ===================================================
    exit /b %ERRORLEVEL%
)
