param([Parameter(Mandatory)][string]$Serial,[ValidateRange(1,300)][int]$Seconds=12,[string]$CameraId='0',[string]$Size='1920x1080',[int]$Fps=30,[int]$Rotation=0,[switch]$Mirror,[switch]$VideoOnly,[ValidateRange(0.01,100)][double]$Zoom=1,[string]$Server)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskPipe='\\.\pipe\AWM.Export.'+[guid]::NewGuid().ToString('N')
function Start-TaskProcess([string]$File,[string[]]$InputArguments){
    $taskInfo=[Diagnostics.ProcessStartInfo]::new($File)
    $taskInfo.UseShellExecute=$false;$taskInfo.CreateNoWindow=$true
    $taskInfo.RedirectStandardOutput=$true;$taskInfo.RedirectStandardError=$true
    $taskInfo.Environment['ADB']=Join-Path $taskRoot 'tools\scrcpy-win64-v5.0.1\adb.exe'
    $taskOwnServer=Join-Path $taskRoot 'build-native\scrcpy-server-awm'
    if($Server){$taskInfo.Environment['SCRCPY_SERVER_PATH']=[IO.Path]::GetFullPath($Server)}elseif(Test-Path -LiteralPath $taskOwnServer){$taskInfo.Environment['SCRCPY_SERVER_PATH']=$taskOwnServer}
    foreach($taskArg in $InputArguments){$taskInfo.ArgumentList.Add($taskArg)}
    $taskProcess=[Diagnostics.Process]::Start($taskInfo)
    return @{Process=$taskProcess;Out=$taskProcess.StandardOutput.ReadToEndAsync();Err=$taskProcess.StandardError.ReadToEndAsync()}
}
$taskHost=$null;$taskScrcpy=$null
try{
    $taskHostArgs=@('--pipe',$taskPipe,'--duration',"$Seconds",'--rotate',"$Rotation")
    if(!$VideoOnly){$taskHostArgs+=@('--microphone','--allow-missing-microphone')}
    if($Mirror){$taskHostArgs+='--mirror'}
    $taskHost=Start-TaskProcess (Join-Path $taskRoot 'build-native\awm-media-host.exe') $taskHostArgs
    Start-Sleep -Milliseconds 350
    $taskZoom=$Zoom.ToString('0.000',[Globalization.CultureInfo]::InvariantCulture)
    $taskArgs=@('--serial',$Serial,'--video-source=camera',"--camera-id=$CameraId","--camera-size=$Size","--camera-fps=$Fps","--camera-zoom=$taskZoom",'--video-codec=h264','--video-bit-rate=16M','--no-playback','--no-window','--no-control','--no-downsize-on-error',"--record=awm-pipe:$taskPipe",'--record-format=mkv','--verbosity=debug')
    if($VideoOnly){$taskArgs+='--no-audio'}else{$taskArgs+=@('--audio-source=mic','--audio-codec=raw','--require-audio')}
    $taskScrcpy=Start-TaskProcess (Join-Path $taskRoot 'build-native\scrcpy-awm.exe') $taskArgs
    if(!$taskHost.Process.WaitForExit(($Seconds+20)*1000)){throw 'Headless test timeout'}
    $taskHostText=$taskHost.Out.Result
    Write-Output $taskHostText
    Write-Output $taskHost.Err.Result
    if(!$taskScrcpy.Process.WaitForExit(3000)){$taskScrcpy.Process.Kill($true);$taskScrcpy.Process.WaitForExit()}
    Write-Output ($taskScrcpy.Out.Result.Replace($Serial,'[selected-phone]'))
    Write-Output ($taskScrcpy.Err.Result.Replace($Serial,'[selected-phone]'))
    if($taskHost.Process.ExitCode){throw "Media host exited $($taskHost.Process.ExitCode)"}
    if($taskHostText -notmatch 'REAL CAPTURE: video_frames=([1-9][0-9]*) '){throw 'Probe received no video frames; no capture success is asserted'}
}finally{
    foreach($taskChild in @($taskScrcpy,$taskHost)){if($taskChild -and !$taskChild.Process.HasExited){$taskChild.Process.Kill($true);$taskChild.Process.WaitForExit()}}
}
