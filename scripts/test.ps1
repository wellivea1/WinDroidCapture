param([switch]$SkipBuild)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
if(!$SkipBuild){& (Join-Path $PSScriptRoot 'build.ps1')}
$taskBuild=Join-Path $taskRoot 'build-native'
$taskResults=Join-Path $taskBuild 'test-results'
New-Item -ItemType Directory -Path $taskResults -Force | Out-Null
foreach($taskTarget in @('awm-native-tests','awm-screen-tests','awm-device-profile-tests','awm-native-lifecycle-tests','awm-render-tests','awm-preview-pacing-tests','awm-video-queue-tests')){
    & (Join-Path $taskBuild ($taskTarget+'.exe')) | Tee-Object -FilePath (Join-Path $taskResults ($taskTarget+'.txt'))
    if($LASTEXITCODE){throw "$taskTarget failed with exit $LASTEXITCODE. Stop an active phone session before shared-transport tests."}
}
& (Join-Path $taskBuild 'awm-camera-tests.exe') (Join-Path $taskBuild 'awm-camera-source.dll') | Tee-Object -FilePath (Join-Path $taskResults 'camera-source.txt')
if($LASTEXITCODE){throw 'Camera component tests failed'}
$taskPython=(Get-Command python -ErrorAction SilentlyContinue).Source
if(!$taskPython){throw 'Python is required for timing-attribution tests'}
& $taskPython (Join-Path $taskRoot 'tests\test_timing_plot.py')
if($LASTEXITCODE){throw 'Timing-attribution fixtures failed'}
Write-Output 'Automated checks passed. Fake process/ring tests and synthetic GPU tests do not establish phone or Discord delivery.'
