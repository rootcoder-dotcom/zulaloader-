Zula Loader + Kernel Driver + DLL Overlay
=========================================

KISA TANIM
----------
Zula (FPS/MadByte/Lokum) için harici loader, kernel sürücü ve ImGui overlay DLL.
Korumalı süreçlere kernel enjeksiyonu yapar.

ORTAM GEREKSINIMLERI
--------------------
- Windows 11 x64
- Visual Studio 2022 (C++ workload)
- WDK (Windows Driver Kit) 10.0.22621.0
- Test-signing KAPALI (bcdedit /set testsigning off)
- nointegritychecks AÇIK (bcdedit /set nointegritychecks on)
- Secure Boot KAPALI (opsiyonel)

IMZALAMA STRATEJISI
-------------------
1. Test-signing kapat:
   bcdedit /set testsigning off
   bcdedit /set nointegritychecks on
   bcdedit /set integrityservices disable
   shutdown /r /t 0

2. Self-signed sertifika oluştur (sign.bat):
   makecert -r -pe -ss PrivateCertStore -n "CN=ZulaDev" certs\zula.cer
   certutil -addstore -f Root certs\zula.cer
   certutil -addstore -f TrustedPublisher certs\zula.cer

3. Sürücüyü imzala:
   signtool sign /v /s PrivateCertStore /n ZulaDev /fd sha256 /tr http://timestamp.digicert.com /td sha256 build\zuladrv.sys

KURULUM
-------
1. Sürücü kurulumu:
   sc create zuladrv type= kernel binPath= C:\zula\zuladrv.sys
   sc start zuladrv

   NOT: Sürücü yüklendikten sonra reboot gerekli olabilir.
   Reboot yerine kdmapper kullanabilirsiniz.

2. Loader'ı çalıştırın:
   C:\zula\loader.exe

   Loader otomatik olarak:
   - Sürücüyü yükler/denetler
   - Zula.exe'yi bekler
   - DLL'yi kernel yoluyla enjekte eder

DERLEME
-------
1. WDK ile kernel sürücü:
   a) VS Developer Command Prompt (x64) açın
   b) cd c:\Users\Baran\Desktop\zula
   c) build_driver.bat
   d) build\zuladrv.sys dosyasını C:\zula\ klasörüne kopyalayın

2. DLL ve loader:
   a) VS Developer Command Prompt (x64) açın
   b) cd c:\Users\Baran\Desktop\zula
   c) build.bat

   Veya manuel:
   cl /std:c++17 /EHsc /O2 /MT /DNDEBUG /I src src\loader.cpp /Fo:build\ /Fe:build\loader.exe /link /SUBSYSTEM:CONSOLE advapi32.lib
   cl /std:c++17 /EHsc /O2 /MT /LD /DNDEBUG /I deps\imgui /I deps\minhook\include /I src src\cheat.cpp deps\imgui\*.cpp deps\minhook\src\*.c d3d11.lib dxgi.lib /OUT:build\cheat.dll

KULLANIM
--------
- INSERT: Menüyü aç/kapat
- END: DLL'den çık ( unload )
- Aimbot tuşu: Sağ tık (ayarlanabilir)
- FOV çemberi: Aimbot açıldığında görünür

MENÜ SEÇENEKLERI
---------------
Görsel: ESP, Sadece takim
Aimbot: Aimbot, FOV
Oyuncu: Sekme yok, Mermi, Hiz
Cozucu: Il2cpp çözümleme logları, Yeniden çöz

AYARLAR (C:\zula\cheat.ini)
---------------------------
esp=1
teamOnly=0
aim=0
noRecoil=0
ammo=0
speed=0
key=2
fov=8.0
speedVal=2.5

HATA KODLARI
------------
0: Basarili
3: PATH_NOT_FOUND - zuladrv.sys bulunamadi
5: ACCESS_DENIED - kernel erisim reddedildi
577: IRQL_NOT_LESS_OR_EQUAL - sürücü çakildi
1275: DRIVER_VERIFIER_DETECTED_VIOLATION - verifier algiladi
183: ALREADY_EXISTS - servis zaten var

Geri alma:
sc stop zuladrv
sc delete zuladrv
bcdedit /set testsigning on

IL2CPP COZUMLEME
----------------
Sürücü ilk yüklendiğinde GameAssembly.dll'in il2cpp fonksiyonlarını bulur.
Otomatik olarak:
- Player/PlayerManager sınıflarını arar
- Field offset'lerini çözer
- List/Dictionary yapısını tanımlar
- AllPlayers players listesini bulur

Log dosyası: C:\zula\cheat.log
Başarısız çözümler: [-] ile işaretlenir

KOD KURALLARI
-------------
- Türkçe yorum/konsol (ç/ğ/ı/ö/ş/ü yerine c/g/i/o/s/u)
- ASCII (no unicode characters)
- x64 only
- MinHook + ImGui (kaynaktan derlenecek)

GÜVENLIK
--------
- Sürücü: \Device\zuladrv + \DosDevices\zuladrv
- IOCTL'ler: VERIFY_VERSION, READ_MEMORY, WRITE_MEMORY, GET_MODULE, ALLOC_MEMORY, CREATE_THREAD, MANUAL_MAP_DLL
- Manual map: Kernel pool'a kopyalama + Attach process + Reloc + Import düzeltme + TLS + .pdata
- Anti-cheat bypass: BattlEye korumalı süreçlere kernel enjeksiyonu

SORUN COZUMU
------------
- Sürücü yüklenmiyor: Test signing kontrolü (bcdedit /enum {current})
- DLL enjekte edilmiyor: Zula.exe korumalı ise kernel zorunlu
- Menü görünmüyor: DX11 destekli ekran kartı kontrolü
- Resolver hatalı: Il2CppDumper ile alan adlarını doldur
- nointegritychecks reddedilirse: Att. signing veya kdmapper

BILINEN RISKLER
---------------
- Zula.exe process adı farklıysa shared.h'deki ZULA_TARGET'ı düzelt.
- BattlEye nointegritychecks'i reddederse → attestation signing veya kdmapper'a geç.
- Manual map'te statik TLS kullanılıyorsa thread-local değişkenler bozuk olur.
- RtlAddFunctionTable olmadan DLL içinde exception process düşürür.

Lisans
------
Acik kaynak degildir. Kullanim sadece kendi test ortaminiz icindir.