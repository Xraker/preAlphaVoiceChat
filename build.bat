@echo off
setlocal enabledelayedexpansion

echo ===================================================
echo  Building preAlphaVoiceChat with Clang and libjuice...
echo ===================================================

clang main.c libjuice/src/*.c -o voicechat.exe ^
    -Ilibjuice/include/juice -Ilibjuice/include -Ilibjuice/src ^
    -DJUICE_STATIC -D_CRT_SECURE_NO_WARNINGS ^
    -lws2_32 -lbcrypt -lavrt -lole32 -luser32 -lgdi32 -lcomctl32 -loleaut32 -lwinhttp ^
    -Xlinker /subsystem:windows ^
    -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-deprecated-declarations

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
