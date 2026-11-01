@echo off
setlocal
cd /d "%~dp0"

if not exist CANKAO mkdir CANKAO

build\Release\AirportEnhancementCaptureReference.exe ^
  --camera 0 ^
  --width 1920 ^
  --height 1080 ^
  --fps 30 ^
  --output "CANKAO\camera_reference_latest.png" ^
  --backup-dir "CANKAO" ^
  --settings "CANKAO\camera_locked_settings.txt" ^
  --preview 1 ^
  --autofocus 1 ^
  --preview-scale 0.5

if %ERRORLEVEL% EQU 0 (
  echo Reference image captured successfully.
) else if %ERRORLEVEL% EQU 2 (
  echo Reference image capture canceled.
) else (
  echo Reference image capture failed.
)

endlocal
