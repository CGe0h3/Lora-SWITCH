@echo off
setlocal EnableExtensions EnableDelayedExpansion
title LoRa-SWITCH - Mise a jour MeshCore
cd /d "%~dp0"

set "ESPTOOL=%~dp0tools\windows\esptool.exe"
set "IMAGE=%~dp0firmware\meshcore-1.18.0-slot1.bin"
set "BAUD=921600"
rem Heltec V4 = native ESP32-S3 USB ("USB Serial Device"): usb_reset puts it in download mode
set "BEFORE=usb_reset"

if not exist "%ESPTOOL%" (
    echo [ERREUR] tools\windows\esptool.exe introuvable.
    echo Dezippe TOUT le dossier avant de lancer ce fichier.
    pause & exit /b 1
)
if not exist "%IMAGE%" (
    echo [ERREUR] firmware\meshcore-1.18.0-slot1.bin introuvable.
    pause & exit /b 1
)

cls
echo ============================================================
echo    LoRa-SWITCH  -  MISE A JOUR DE MESHCORE SEULEMENT
echo ============================================================
echo.
echo    Remplace uniquement MeshCore (emplacement 1) par la version
echo    1.18.0 corrigee (adresse Bluetooth distincte de Meshtastic).
echo.
echo    Meshtastic, le menu et TOUS les reglages sont conserves.
echo    A utiliser seulement si le multi-boot est deja installe.
echo.

:ports
echo ------------------------------------------------------------
echo  Ports serie detectes :
echo.
powershell -NoProfile -Command "$p = Get-PnpDevice -Class Ports -PresentOnly -ErrorAction SilentlyContinue; if ($p) { $p | ForEach-Object { '     ' + $_.FriendlyName } }"
set "DEFPORT="
set /a N=0
for /f "delims=" %%p in ('powershell -NoProfile -Command "Get-PnpDevice -Class Ports -PresentOnly -ErrorAction SilentlyContinue | Where-Object { $_.FriendlyName -match 'USB' } | ForEach-Object { [regex]::Match($_.FriendlyName, 'COM[0-9]+').Value } | Where-Object { $_ }"') do (
    set /a N+=1
    set "DEFPORT=%%p"
)
echo.
if !N! EQU 0 (
    echo  Aucun port USB trouve.
    echo   - Branche la carte avec un cable USB-C qui transmet les DONNEES
    echo   - Refais le mode programmation : debranche, maintiens PRG,
    echo     rebranche, relache PRG
    echo.
    echo  Appuie sur une touche pour rechercher a nouveau...
    pause >nul
    goto ports
)

set "PORT="
if !N! EQU 1 (
    set /p "PORT= Port COM a utiliser [Entree = !DEFPORT!] : "
    if "!PORT!"=="" set "PORT=!DEFPORT!"
) else (
    set /p "PORT= Plusieurs ports USB : tape celui de la carte (ex: COM16) : "
)
if "!PORT!"=="" goto ports
rem "5" -> "COM5"
if /I not "!PORT:~0,3!"=="COM" set "PORT=COM!PORT!"
rem esptool needs "COM16", not "com16" (else no USB auto-reset)
set "PORT=!PORT:com=COM!"
set "PORT=!PORT: =!"

echo.
choice /c ON /n /m " Mettre a jour MeshCore sur !PORT! ? [O/N] "
if errorlevel 2 (
    echo Annule.
    pause & exit /b 0
)

:flash
echo.
echo ------------------------------------------------------------
echo  Ecriture de MeshCore en cours (environ 30 secondes)...
echo  Ne debranche pas la carte.
echo ------------------------------------------------------------
"%ESPTOOL%" --chip esp32s3 -p !PORT! -b !BAUD! --before !BEFORE! --after hard_reset write_flash --flash_mode keep --flash_size keep 0x3C0000 "%IMAGE%"
if errorlevel 1 goto fail

echo.
echo ============================================================
echo    MISE A JOUR TERMINEE
echo ============================================================
echo.
echo    1. Sur l'iPhone : Reglages ^> Bluetooth ^> "Oublier cet appareil"
echo       pour l'entree MeshCore de cette carte (si elle existe)
echo    2. Appuie sur RST, choisis MeshCore dans le menu
echo    3. Dans l'appli MeshCore, la carte apparait comme un NOUVEL
echo       appareil : connecte-toi et saisis le code affiche a l'ecran
echo.
pause
exit /b 0

:fail
echo.
echo ============================================================
echo    ECHEC DE LA MISE A JOUR
echo ============================================================
echo.
echo    Essaie ceci :
echo     1. Ferme tout ce qui utilise le port (client web Meshtastic,
echo        appli MeshCore sur PC, Arduino IDE...)
echo     2. Choisis [M] : mode programmation manuel (bouton PRG)
echo     3. Change de cable ou de port USB
echo.
choice /c RVMQ /n /m " [R]ecommencer  [V]itesse reduite  [M]ode manuel  [Q]uitter : "
if errorlevel 4 exit /b 1
if errorlevel 3 goto manual
if errorlevel 2 set "BAUD=115200"
goto flash

:manual
set "BEFORE=no_reset"
set "BAUD=115200"
echo ------------------------------------------------------------
echo  ETAPE 1 : METTRE LA CARTE EN MODE PROGRAMMATION
echo ------------------------------------------------------------
echo   1. Debranche le cable USB de la carte
echo   2. MAINTIENS le bouton PRG enfonce
echo   3. Rebranche le cable USB sans relacher PRG
echo   4. Attends 2 secondes, puis relache PRG
echo   5. L'ecran doit rester NOIR (sinon, recommence)
echo.
echo  Quand l'ecran est noir, appuie sur une touche pour continuer...
echo  (n'ouvre rien d'autre entre-temps)
pause >nul
goto flash

