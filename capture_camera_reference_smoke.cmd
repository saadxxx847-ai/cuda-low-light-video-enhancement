@echo off
setlocal
cd /d "%~dp0"

if not exist CANKAO mkdir CANKAO
if not exist codex_run_logs_realtime mkdir codex_run_logs_realtime

build\Release\AirportEnhancementCaptureReference.exe ^
  --camera 0 ^
  --width 1920 ^
  --height 1080 ^
  --fps 30 ^
  --output "CANKAO\camera_reference_latest.png" ^
  --backup-dir "CANKAO" ^
  --settings "CANKAO\camera_locked_settings.txt" ^
  --preview 0 ^
  --autofocus 1 ^
  --auto-capture 2 ^
  > "codex_run_logs_realtime\capture_camera_reference_smoke.log" 2>&1

echo %ERRORLEVEL% > "codex_run_logs_realtime\capture_camera_reference_smoke.status"
endlocal
