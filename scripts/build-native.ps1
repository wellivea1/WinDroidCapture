param([string[]]$Targets=@())
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
$taskVswhere=Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio\Installer\vswhere.exe'
$taskVs=& $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$taskVs){throw 'MSVC x64 desktop tools are required'}
$taskVcvars=Join-Path $taskVs 'VC\Auxiliary\Build\vcvars64.bat'
$taskLines=& $env:ComSpec /d /c ('call "{0}" >nul && set' -f $taskVcvars)
if($LASTEXITCODE){throw 'Could not initialize the compiler environment'}
foreach($taskLine in $taskLines){if($taskLine -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$taskPython=(Get-Command python -ErrorAction SilentlyContinue).Source
if(!$taskPython){$taskPython='C:\Python314\python.exe'}
& (Join-Path $PSScriptRoot 'build-camera-server.ps1')
& $taskPython (Join-Path $PSScriptRoot 'prepare-native.py')
if($LASTEXITCODE){throw 'Native dependency preparation failed'}
$taskCmake=Join-Path $taskVs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$taskNinja=Join-Path $taskVs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$taskClang='C:\Program Files\LLVM\bin\clang-cl.exe'
if(!(Test-Path -LiteralPath $taskClang)){throw 'Install official LLVM with clang-cl for the upstream C client GNU extensions'}
& $taskCmake -S $taskRoot -B (Join-Path $taskRoot 'build-native') -G Ninja "-DCMAKE_MAKE_PROGRAM=$taskNinja" "-DCMAKE_C_COMPILER=$taskClang" -DCMAKE_BUILD_TYPE=Release -DAWM_NATIVE=ON
if($LASTEXITCODE){throw 'Native configure failed'}
if($Targets.Count){& $taskCmake --build (Join-Path $taskRoot 'build-native') --target @Targets}else{& $taskCmake --build (Join-Path $taskRoot 'build-native')}
if($LASTEXITCODE){throw 'Native build failed'}
