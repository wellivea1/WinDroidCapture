param([string[]]$Targets=@())
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'build-native.ps1') -Targets $Targets
if($LASTEXITCODE){throw 'Native build failed'}
