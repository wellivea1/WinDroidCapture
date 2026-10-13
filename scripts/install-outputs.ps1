param([switch]$Camera,[string]$SignedMicrophonePackage)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskBuild=Join-Path $taskRoot 'build-native'
if(!(Test-Path $taskBuild)){$taskBuild=$taskRoot}
if($Camera){
    $taskProcess=Start-Process -FilePath (Join-Path $taskBuild 'awm-camera-control.exe') -ArgumentList 'install-source' -Verb RunAs -WindowStyle Hidden -PassThru -Wait
    if($taskProcess.ExitCode){throw "Camera source install failed: $($taskProcess.ExitCode)"}
    & (Join-Path $taskBuild 'awm-camera-control.exe') register-user
    if($LASTEXITCODE){throw 'Current-user camera registration failed'}
}
if($SignedMicrophonePackage){
    $taskFolder=[IO.Path]::GetFullPath($SignedMicrophonePackage)
    $taskSignature=Get-AuthenticodeSignature -LiteralPath (Join-Path $taskFolder 'awm_mic.cat')
    if($taskSignature.Status -ne 'Valid'){throw 'Refused: supply a valid Microsoft-signed driver catalog. No test-signing/security changes are made.'}
    # Windows paths cannot contain double quotes. Escape a trailing backslash
    # before the closing quote for the native argv parser.
    $taskQuoted='"'+($taskFolder -replace '(\\+)$','$1$1')+'"'
    $taskProcess=Start-Process -FilePath (Join-Path $taskBuild 'awm-microphone-install.exe') -ArgumentList $taskQuoted -Verb RunAs -WindowStyle Hidden -PassThru -Wait
    if($taskProcess.ExitCode){throw "Windows microphone installation failed: $($taskProcess.ExitCode)"}
}
