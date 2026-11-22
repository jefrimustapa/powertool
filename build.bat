@echo off
cd /d "%~dp0"
where gcc >nul 2>nul
if errorlevel 1 (
    if exist "%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe" (
        set "PATH=%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin;%PATH%"
    )
)
windres -O coff powertool.rc -o powertool-res.o
if errorlevel 1 goto fail
gcc -O2 -Wall -municode -mwindows powertool.c powertool-res.o -o powertool.exe -lshell32 -lcomdlg32 -ladvapi32 -luser32 -lpowrprof
if errorlevel 1 goto fail
del powertool-res.o
echo Built powertool.exe
exit /b 0
:fail
echo Build failed.
exit /b 1
