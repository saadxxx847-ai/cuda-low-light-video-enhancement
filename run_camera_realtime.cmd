@echo off
setlocal
cd /d "%~dp0"

if not exist outvideo_camera_realtime mkdir outvideo_camera_realtime

if not exist "CANKAO\camera_reference_latest.png" (
  echo Missing reference image: CANKAO\camera_reference_latest.png
  echo Please run capture_camera_reference.cmd under normal lighting first.
  pause
  exit /b 1
)

if not exist "CANKAO\camera_locked_settings.txt" (
  echo Missing locked camera settings: CANKAO\camera_locked_settings.txt
  echo Please run capture_camera_reference.cmd under normal lighting first.
  pause
  exit /b 1
)

build\Release\AirportEnhancementCameraRealtime.exe ^
  --camera 0 ^
  --width 1920 ^
  --height 1080 ^
  --fps 30 ^
  --reference "CANKAO\camera_reference_latest.png" ^
  --settings "CANKAO\camera_locked_settings.txt" ^
  --output "outvideo_camera_realtime\camera_pic1L_realtime.mp4" ^
  --preview 1 ^
  --record 1 ^
  --autofocus 1 ^
  --preview-scale 0.42 ^
  --smooth 0.35

endlocal
