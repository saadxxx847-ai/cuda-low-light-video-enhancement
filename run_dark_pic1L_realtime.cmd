@echo off
setlocal
cd /d "%~dp0"

if not exist outvideo_realtime_Dark mkdir outvideo_realtime_Dark
if not exist codex_run_logs_realtime mkdir codex_run_logs_realtime

build\Release\AirportEnhancement_remake_.exe ^
  "original_Video_of_the_airport\dark.mp4" ^
  "CANKAO\pic1L.png" ^
  "outvideo_realtime_Dark\pic1L_dark_realtime.mp4" ^
  > "codex_run_logs_realtime\pic1L_dark_realtime.log" 2>&1

echo %ERRORLEVEL% > "codex_run_logs_realtime\pic1L_dark_realtime.status"
endlocal
