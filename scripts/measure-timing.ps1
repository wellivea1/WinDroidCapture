param([Parameter(Mandatory)][string]$ScrcpyFolder,[string]$OutputDirectory,[string]$Python='python',[ValidateSet(30,60)][int]$ReferenceFPS=30)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot -Parent
if(!$OutputDirectory){$OutputDirectory=Join-Path $taskRoot ('evidence\timing-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
$taskTiming=[IO.Path]::GetFullPath($OutputDirectory)
& (Join-Path $PSScriptRoot 'run.ps1') -ScrcpyFolder $ScrcpyFolder -TimingDirectory $taskTiming
foreach($taskFile in @('capture.csv','preview.csv')){
    if(!(Test-Path -LiteralPath (Join-Path $taskTiming $taskFile))){throw "No $taskFile. Select a phone, start a real capture and keep its preview visible before closing the application."}
}
& $Python (Join-Path $PSScriptRoot 'plot-timing.py') --capture (Join-Path $taskTiming 'capture.csv') --preview (Join-Path $taskTiming 'preview.csv') --out (Join-Path $taskTiming 'graphs') --fps $ReferenceFPS
if($LASTEXITCODE){throw 'Plotting failed. Install matplotlib and numpy in the selected Python; raw traces remain available.'}
Write-Output "Graphs: $(Join-Path $taskTiming 'graphs')"
