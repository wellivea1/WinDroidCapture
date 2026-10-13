param([switch]$AddToUserPath,[string]$Version='5.0.1')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$headers=@{'User-Agent'='WinDroidCapture-setup';'Accept'='application/vnd.github+json'}
if($Version -ne 'latest' -and $Version -notmatch '^\d+\.\d+\.\d+$'){throw 'Version must be an exact release such as 5.0.1, or latest'}
$releaseUrl=if($Version -eq 'latest'){'https://api.github.com/repos/Genymobile/scrcpy/releases/latest'}else{'https://api.github.com/repos/Genymobile/scrcpy/releases/tags/v'+$Version}
$release=Invoke-RestMethod -Uri $releaseUrl -Headers $headers
$assets=@($release.assets | Where-Object {$_.name -match '^scrcpy-win64-v[0-9.]+\.zip$'})
if($assets.Count -ne 1){throw 'The official release has no unique Windows x64 archive; select a distribution manually in Setup.'}
$asset=$assets[0]
if($asset.browser_download_url -notmatch '^https://github\.com/Genymobile/scrcpy/releases/download/'){throw 'Unexpected publisher download URL'}
$digest=[regex]::Match([string]$asset.digest,'^sha256:([a-fA-F0-9]{64})$')
if(!$digest.Success){throw 'No published SHA256 digest is available. Refusing an unverified automatic download; use the official release page manually.'}
$expected=$digest.Groups[1].Value.ToLowerInvariant()
$tools=Join-Path $root 'tools'
$destination=Join-Path $tools ($asset.name -replace '\.zip$','')
$archive=Join-Path $tools $asset.name
if((Test-Path -LiteralPath $destination) -or (Test-Path -LiteralPath $archive)){throw "Managed destination already exists: $destination. Nothing was replaced. Select it in the application or choose a separate installation."}
New-Item -ItemType Directory -Path $tools -Force | Out-Null
Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $archive -UseBasicParsing
$actual=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
if($actual -ne $expected){throw "SHA256 mismatch. Archive was not extracted: $archive"}
# The official archive contains its named top-level distribution folder.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::OpenRead($archive)
try{foreach($entry in $zip.Entries){
    $resolved=[IO.Path]::GetFullPath((Join-Path $tools $entry.FullName))
    $prefix=[IO.Path]::GetFullPath($destination)+'\'
    if($resolved -ne [IO.Path]::GetFullPath($destination) -and !$resolved.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Archive layout escapes the new distribution folder; nothing was extracted.'}
}}finally{$zip.Dispose()}
Expand-Archive -LiteralPath $archive -DestinationPath $tools
if(!(Test-Path -LiteralPath (Join-Path $destination 'scrcpy.exe')) -or !(Test-Path -LiteralPath (Join-Path $destination 'adb.exe'))){throw 'Verified archive has an unexpected layout; select its actual paths manually.'}
[pscustomobject]@{Release=$release.tag_name;Path=$destination;SHA256=$actual;Source=$asset.browser_download_url} | Format-List
foreach($exe in @('scrcpy.exe','adb.exe')){
    $signature=Get-AuthenticodeSignature -LiteralPath (Join-Path $destination $exe)
    Write-Host "$exe Authenticode: $($signature.Status). Archive authenticity was checked against the publisher's published digest."
}
if($AddToUserPath){
    $existing=[string][Environment]::GetEnvironmentVariable('Path','User')
    if(@($existing -split ';') -notcontains $destination){[Environment]::SetEnvironmentVariable('Path',(($existing.TrimEnd(';')+';'+$destination).TrimStart(';')),'User')}
    Write-Host 'Added this distribution to the user PATH. New terminals pick up the change. Existing selected installations were not replaced.'
}
Write-Host 'Choose this extracted folder in WinDroid Capture. The same selected adb.exe is passed to every scrcpy child.'
