@echo off
echo Test signing ve integrity ayarlari...
bcdedit /set testsigning off
bcdedit /set nointegritychecks on
bcdedit /set integrityservices disable
echo Sistem yeniden baslatiliyor...
shutdown /r /t 5