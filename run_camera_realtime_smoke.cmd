@echo off
setlocal
cd /d "%~dp0"

if not exist outvideo_camera_realtime mkdir outvideo_camera_realtime
if not exist codex_run_logs_realtime mkdir codex_run_logs_realtime

build\Release\AirportEnhancementCameraRealtime.exe ^
  --camera 0 ^
  --width 1920 ^
  --height 1080 ^
  --fps 30 ^
  --reference "CANKAO\camera_reference_latest.png" ^
  --settings "CANKAO\camera_locked_settings.txt" ^
  --output "outvideo_camera_realtime\camera_pic1L_smoke_1080p.mp4" ^
  --preview 0 ^
  --record 1 ^
  --duration 8 ^
  --autofocus 1 ^
  --smooth 0.35 ^
  > "codex_run_logs_realtime\camera_realtime_smoke.log" 2>&1

echo %ERRORLEVEL% > "codex_run_logs_realtime\camera_realtime_smoke.status"
endlocal
