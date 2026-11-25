@echo off
setlocal
set CDB=C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe
"%CDB%" -z C:\WINDOWS\Minidump\112526-9968-01.dmp -c "!analyze -v; qd" > "C:\Users\Baran\Desktop\zula\bsod_analysis.txt" 2>&1
exit /b 0
