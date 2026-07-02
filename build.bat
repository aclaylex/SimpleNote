@echo off
REM Build script for SimpleNote.
REM Run this from the "x64 Native Tools Command Prompt for VS 2026".

echo Compiling resource file...
rc notepad.rc
if errorlevel 1 goto :error

echo Compiling and linking...
cl /O1 /MD /W3 notepad.c notepad.res user32.lib gdi32.lib comdlg32.lib dwmapi.lib advapi32.lib /link /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /MANIFEST:NO /OUT:SimpleNote.exe
if errorlevel 1 goto :error

echo.
echo Build succeeded! SimpleNote.exe is ready.
dir SimpleNote.exe
goto :end

:error
echo.
echo Build FAILED. See errors above.

:end
