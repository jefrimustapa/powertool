@echo off
cd /d "%~dp0"
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
