@echo off
setlocal
set CDB=C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe
(
echo ===== dt nt!_OBJECT_HEADER
"%CDB%" -z C:\WINDOWS\Minidump\112526-9968-01.dmp -c "dt nt!_OBJECT_HEADER; qd"
echo ===== u nt!ObfReferenceObjectWithTag
"%CDB%" -z C:\WINDOWS\Minidump\112526-9968-01.dmp -c "u nt!ObfReferenceObjectWithTag L60; qd"
echo ===== dt nt!_DRIVER_OBJECT
"%CDB%" -z C:\WINDOWS\Minidump\112526-9968-01.dmp -c "dt nt!_DRIVER_OBJECT; qd"
) > "C:\Users\Baran\Desktop\zula\bsod_dump_info.txt" 2>&1
exit /b 0
