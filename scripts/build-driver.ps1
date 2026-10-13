param()
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskVswhere=Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$taskVs=& $taskVswhere -latest -products '*' -property installationPath
$taskMsbuild=Join-Path $taskVs 'MSBuild\Current\Bin\MSBuild.exe'
& $taskMsbuild (Join-Path $taskRoot 'drivers\microphone\awm_mic.vcxproj') /m /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo
if($LASTEXITCODE){throw 'C microphone driver build failed'}
Copy-Item -LiteralPath (Join-Path $taskRoot 'drivers\microphone\awm_mic.inf') -Destination (Join-Path $taskRoot 'build-driver\awm_mic.inf')
$taskInfVerif='C:\Program Files (x86)\Windows Kits\10\Tools\10.0.28000.0\x64\infverif.exe'
& $taskInfVerif /w (Join-Path $taskRoot 'build-driver\awm_mic.inf')
if($LASTEXITCODE){throw 'Driver INF validation failed'}
& 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x86\Inf2Cat.exe' ('/driver:'+ (Join-Path $taskRoot 'build-driver')) /os:10_GE_X64
if($LASTEXITCODE){throw 'Driver catalog generation failed'}
Write-Output 'Built development kernel driver. This is not a Microsoft-signed installation package.'
