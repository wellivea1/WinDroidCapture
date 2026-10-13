param([string]$Jdk,[string]$Platform,[string]$BuildTools)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
if(!$Jdk){$Jdk=(Get-ChildItem -LiteralPath (Join-Path $taskRoot 'tools\openjdk21') -Directory | Select-Object -First 1).FullName}
if(!$Platform){$Platform=Join-Path $taskRoot 'tools\android-sdk-parts\platforms-android-36\android-36'}
if(!$BuildTools){$BuildTools=Join-Path $taskRoot 'tools\android-sdk-parts\build-tools-36.0.0\android-16'}
foreach($taskRequired in @((Join-Path $Jdk 'bin\javac.exe'),(Join-Path $Jdk 'bin\java.exe'),(Join-Path $Platform 'android.jar'),(Join-Path $BuildTools 'aidl.exe'),(Join-Path $BuildTools 'lib\d8.jar'))){if(!(Test-Path -LiteralPath $taskRequired)){throw "Missing Android server build input: $taskRequired"}}
& python (Join-Path $PSScriptRoot 'prepare-camera-server.py')
if($LASTEXITCODE){throw 'Server extension preparation failed'}
$taskBuild=Join-Path $taskRoot 'build-native\android-server'
$taskSources=Join-Path $taskBuild 'sources'
$taskGenerated=Join-Path $taskBuild 'generated'
$taskClasses=Join-Path $taskBuild 'classes'
$taskDex=Join-Path $taskBuild 'dex'
New-Item -ItemType Directory -Path $taskClasses,$taskDex -Force | Out-Null
foreach($taskAidl in Get-ChildItem -LiteralPath (Join-Path $taskSources 'aidl') -Filter *.aidl -Recurse -File){
    & (Join-Path $BuildTools 'aidl.exe') ('-p'+(Join-Path $Platform 'framework.aidl')) ('-I'+(Join-Path $taskSources 'aidl')) ('-o'+$taskGenerated) $taskAidl.FullName
    if($LASTEXITCODE){throw 'Upstream AIDL generation failed'}
}
$taskJava=@(Get-ChildItem -LiteralPath (Join-Path $taskSources 'java'),$taskGenerated -Filter *.java -Recurse -File | ForEach-Object {'"'+$_.FullName.Replace('\','/')+'"'})
$taskJavaList=Join-Path $taskBuild 'java-sources.txt'
[IO.File]::WriteAllLines($taskJavaList,$taskJava,[Text.UTF8Encoding]::new($false))
& (Join-Path $Jdk 'bin\javac.exe') -encoding UTF-8 -source 17 -target 17 -classpath (Join-Path $Platform 'android.jar') -d $taskClasses ('@'+$taskJavaList)
if($LASTEXITCODE){throw 'Upstream server Java compilation failed'}
$taskJar=Join-Path $taskBuild 'server-classes.jar'
& (Join-Path $Jdk 'bin\jar.exe') --create --file $taskJar -C $taskClasses .
if($LASTEXITCODE){throw 'Server class packaging failed'}
& (Join-Path $Jdk 'bin\java.exe') -cp (Join-Path $BuildTools 'lib\d8.jar') com.android.tools.r8.D8 --release --min-api 21 --lib (Join-Path $Platform 'android.jar') --output $taskDex $taskJar
if($LASTEXITCODE){throw 'Server DEX compilation failed'}
$taskServer=Join-Path $taskRoot 'build-native\scrcpy-server-awm'
& (Join-Path $Jdk 'bin\jar.exe') --create --file $taskServer -C $taskDex classes.dex
if($LASTEXITCODE){throw 'Server DEX packaging failed'}
$taskManifest=Get-Content -LiteralPath (Join-Path $taskBuild 'server-source-manifest.json') -Raw | ConvertFrom-Json
$taskManifest | Add-Member -NotePropertyName sha256 -NotePropertyValue (Get-FileHash -LiteralPath $taskServer -Algorithm SHA256).Hash.ToLowerInvariant()
$taskManifest | Add-Member -NotePropertyName java -NotePropertyValue ((& (Join-Path $Jdk 'bin\java.exe') -version 2>&1) -join ' ')
$taskManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskRoot 'build-native\awm-camera-server.json') -Encoding UTF8
Write-Output "Built separate AWM camera API 1 server: $taskServer"
