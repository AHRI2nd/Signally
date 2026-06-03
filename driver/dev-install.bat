@echo off
REM ============================================================================
REM  Signally Virtual Microphone — development build / sign / install helper.
REM  Run from an *elevated* (Administrator) Developer Command Prompt for VS 2022
REM  with the WDK installed, from the driver\VirtualMicDriver folder.
REM
REM  This is for LOCAL DEVELOPMENT ONLY. It uses a self-signed test certificate
REM  and enables Windows test-signing mode. For distribution use attestation
REM  signing instead (see BUILD.md, section 4).
REM ============================================================================

setlocal
set DRV=VirtualMicDriver
set CERT=SignallyTestCert
set HWID=Root\VirtualMicDriver

echo.
echo [1/5] Building the driver (Release x64)...
msbuild %DRV%.vcxproj /p:Configuration=Release /p:Platform=x64 || goto :error

echo.
echo [2/5] Creating / reusing a self-signed test certificate...
if not exist %CERT%.cer (
    makecert -r -pe -ss PrivateCertStore -n "CN=%CERT%" %CERT%.cer || goto :error
)

echo.
echo [3/5] Signing %DRV%.sys with the test certificate...
signtool sign /v /s PrivateCertStore /n "%CERT%" /fd sha256 ^
    /t http://timestamp.digicert.com x64\Release\%DRV%.sys || goto :error

echo.
echo [4/5] Trusting the test certificate + enabling test signing (reboot needed)...
certutil -addstore -f root %CERT%.cer
certutil -addstore -f trustedpublisher %CERT%.cer
bcdedit /set testsigning on

echo.
echo [5/5] Installing the driver (creates the Root\VirtualMicDriver device node)...
devcon install x64\Release\%DRV%.inf %HWID% || goto :error

echo.
echo Done. REBOOT for testsigning to take effect, then verify with:
echo     devcon status %HWID%
goto :eof

:error
echo.
echo *** FAILED (errorlevel %errorlevel%). See messages above. ***
exit /b %errorlevel%
