param([Parameter(Mandatory)][string]$AdbPath,[Parameter(Mandatory)][ValidateRange(1,2147483647)][int]$TransportId,[Parameter(Mandatory)][string]$TimingDirectory)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskTrace=(Resolve-Path -LiteralPath $TimingDirectory).Path
$taskActivity=& $AdbPath -t $TransportId shell dumpsys activity activities
if(($taskActivity -join "`n") -notmatch 'topResumedActivity=.*com.android.settings'){throw 'The explicitly selected phone must be awake on Settings. No swipes were sent.'}
$taskSize=& $AdbPath -t $TransportId shell wm size
if(($taskSize -join "`n") -notmatch '1344x2992'){throw 'This repeatable motion procedure targets the verified Pixel display size only.'}
$taskStart=[Diagnostics.ProcessStartInfo]::new()
$taskStart.FileName=Join-Path $taskRoot 'build-native\awm-native-receiver.exe'
if(!(Test-Path -LiteralPath $taskStart.FileName)){$taskStart.FileName=Join-Path $taskRoot 'awm-native-receiver.exe'}
$taskStart.WorkingDirectory=Split-Path $taskStart.FileName -Parent
$taskStart.Arguments='--video-only --duration 40 --width 1280 --height 720 --fps 30'
$taskStart.UseShellExecute=$false;$taskStart.CreateNoWindow=$true
$taskStart.RedirectStandardOutput=$true;$taskStart.RedirectStandardError=$true
$taskStart.EnvironmentVariables['AWM_RECEIVER_TRACE_CSV']=Join-Path $taskTrace 'receiver.csv'
$taskReceiver=[Diagnostics.Process]::Start($taskStart)
$taskOut=$taskReceiver.StandardOutput.ReadToEndAsync();$taskErr=$taskReceiver.StandardError.ReadToEndAsync()
$taskBeginning=1000.0*[Diagnostics.Stopwatch]::GetTimestamp()/[Diagnostics.Stopwatch]::Frequency
for($taskIndex=0;$taskIndex -lt 22;$taskIndex++){
    if($taskIndex%2 -eq 0){& $AdbPath -t $TransportId shell input swipe 670 2250 670 950 1000}
    else{& $AdbPath -t $TransportId shell input swipe 670 950 670 2250 1000}
    if($LASTEXITCODE){throw 'Targeted Settings swipe failed'}
}
$taskEnding=1000.0*[Diagnostics.Stopwatch]::GetTimestamp()/[Diagnostics.Stopwatch]::Frequency
@{start_ms=$taskBeginning;end_ms=$taskEnding;swipes=22;source='Pixel Settings, USB, Balanced screen';receiver='explicit Android Webcam Camera, 1280x720/30, video-only';media_recorded=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskTrace 'motion-window.json') -Encoding utf8
$taskReceiver.WaitForExit()
$taskOut.Result | Set-Content -LiteralPath (Join-Path $taskTrace 'receiver-result.txt') -Encoding utf8
$taskErr.Result | Set-Content -LiteralPath (Join-Path $taskTrace 'receiver-errors.txt') -Encoding utf8
Write-Output $taskOut.Result
$taskExit=$taskReceiver.ExitCode;$taskReceiver.Dispose()
if($taskExit){throw "Explicit receiver failed with exit $taskExit; no fallback was used."}
