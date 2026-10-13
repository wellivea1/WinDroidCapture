param([Parameter(Mandatory)][string]$ScrcpyFolder,[string]$TimingDirectory)
$ErrorActionPreference='Stop'
$taskBase=Split-Path $PSScriptRoot -Parent
$taskTools=[IO.Path]::GetFullPath($ScrcpyFolder)
foreach($taskFile in @('adb.exe','scrcpy.exe','scrcpy-server','SDL3.dll','avcodec-63.dll','avformat-63.dll','avutil-61.dll','swresample-7.dll')){
    if(!(Test-Path -LiteralPath (Join-Path $taskTools $taskFile))){throw "Missing matching official runtime file: $taskFile"}
}
$taskExecutable=Join-Path $taskBase 'WinDroidCapture.exe'
if(!(Test-Path -LiteralPath $taskExecutable)){$taskExecutable=Join-Path $taskBase 'build-native\WinDroidCapture.exe'}
# Dependency search changes apply only to the application process and children.
$taskStart=[Diagnostics.ProcessStartInfo]::new()
$taskStart.FileName=$taskExecutable
$taskStart.UseShellExecute=$false
$taskStart.CreateNoWindow=$true
$taskStart.WorkingDirectory=Split-Path $taskExecutable -Parent
$taskStart.EnvironmentVariables['PATH']=$taskTools+';'+$env:PATH
$taskStart.EnvironmentVariables['SCRCPY_SERVER_PATH']=Join-Path $taskTools 'scrcpy-server'
if($TimingDirectory){
    $taskTiming=[IO.Path]::GetFullPath($TimingDirectory)
    if(Test-Path -LiteralPath $taskTiming){throw 'Timing directory already exists. Choose a new directory for this measurement.'}
    New-Item -ItemType Directory -Path $taskTiming | Out-Null
    $taskStart.EnvironmentVariables['AWM_CAPTURE_TRACE_CSV']=Join-Path $taskTiming 'capture.csv'
    $taskStart.EnvironmentVariables['AWM_PREVIEW_TRACE_CSV']=Join-Path $taskTiming 'preview.csv'
    $taskStart.EnvironmentVariables['AWM_CAPTURE_TIMING_LOG']=Join-Path $taskTiming 'capture-summary.txt'
    $taskStart.EnvironmentVariables['AWM_PREVIEW_TIMING_LOG']=Join-Path $taskTiming 'preview-summary.txt'
    Write-Output "Opt-in numeric timing traces: $taskTiming. No media recording. Close the app before plotting."
}
$taskProcess=[Diagnostics.Process]::Start($taskStart)
try {$taskProcess.WaitForExit(); if($taskProcess.ExitCode){throw "Application exited with code $($taskProcess.ExitCode)"}}finally{$taskProcess.Dispose()}
