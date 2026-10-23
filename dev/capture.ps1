param(
    [Parameter(Mandatory=$true, Position=0)][string]$Image,
    [Parameter(Position=1)][string]$Output,
    [ValidateRange(64,8192)][int]$Analysis=2048,
    [ValidateSet('edges','regions')][string]$Mode='edges',
    [ValidateRange(4,256)][int]$Points=16,
    [ValidateRange(0,64)][int]$MinLength=3,
    [ValidateRange(0,100)][int]$Detail=44,
    [ValidateRange(0,2)][double]$Tolerance=1.55,
    [ValidateRange(1,200)][int]$ContrastMultiplier=100,
    [ValidateRange(0,100)][int]$ColorSensitivity=89,
    [ValidateRange(0,100)][int]$BrightnessSensitivity=80,
    [switch]$NoConnect,
    [switch]$Connect,
    [switch]$Noise,
    [switch]$Dev
)
if ($Mode -eq 'edges') {
    if (!$PSBoundParameters.ContainsKey('Detail')) { $Detail=0 }
    if (!$PSBoundParameters.ContainsKey('Tolerance')) { $Tolerance=0.7 }
    if (!$PSBoundParameters.ContainsKey('Points')) { $Points=60 }
}
if ($Mode -eq 'edges') { $NoConnect=!$Connect }
if ($Mode -eq 'regions') {
    if (!$PSBoundParameters.ContainsKey('MinLength')) { $MinLength=6 }
    if (!$PSBoundParameters.ContainsKey('ColorSensitivity')) { $ColorSensitivity=100 }
    if (!$PSBoundParameters.ContainsKey('BrightnessSensitivity')) { $BrightnessSensitivity=100 }
}
if ($Mode -eq 'regions' -and !$PSBoundParameters.ContainsKey('Analysis')) { $Analysis=1024 }
$ErrorActionPreference='Stop'
$taskRoot=Split-Path $PSScriptRoot
$taskInput=(Resolve-Path -LiteralPath $Image).Path
if (!$Output) { $Output=Join-Path $PSScriptRoot ('qa\captures\'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')) }
$taskOutput=[IO.Path]::GetFullPath($Output)
if ($Dev) { $taskExe=Join-Path $PSScriptRoot 'build\Release\ContourStudio.exe' }
else {
    $taskVersion=(Get-Content -LiteralPath (Join-Path $taskRoot 'release\latest.txt') -Raw).Trim()
    if ($taskVersion -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid release version.' }
    $taskExe=Join-Path $taskRoot "release\$taskVersion\ContourStudio.exe"
}
if (!(Test-Path -LiteralPath $taskExe)) { throw 'Build the application first with Sestavit.bat.' }
if ($taskInput.Contains('"') -or $taskOutput.Contains('"')) { throw 'Invalid path.' }
$taskArguments=@('--capture',('"'+$taskInput+'"'),('"'+$taskOutput.TrimEnd('\')+'"'),'--mode',$Mode,'--analysis',$Analysis,'--points',$Points,'--detail',$Detail,'--tolerance',$Tolerance.ToString([Globalization.CultureInfo]::InvariantCulture))
$taskArguments+=@('--contrast-multiplier',($ContrastMultiplier/100.0).ToString([Globalization.CultureInfo]::InvariantCulture),'--color-sensitivity',($ColorSensitivity/100.0).ToString([Globalization.CultureInfo]::InvariantCulture),'--brightness-sensitivity',($BrightnessSensitivity/100.0).ToString([Globalization.CultureInfo]::InvariantCulture))
if ($NoConnect) { $taskArguments+='--no-connect' }
if ($Connect) { $taskArguments+='--connect' }
if ($Noise) { $taskArguments+='--noise' }
$taskArguments+=@('--min-length',$MinLength)
$taskProcess=Start-Process -FilePath $taskExe -ArgumentList $taskArguments -WindowStyle Hidden -Wait -PassThru
if ($taskProcess.ExitCode) {
    $taskError=Join-Path $taskOutput 'capture-error.txt'
    if (Test-Path -LiteralPath $taskError) { throw (Get-Content -LiteralPath $taskError -Raw) }
    throw "Capture failed: exit $($taskProcess.ExitCode)."
}
Get-Content -LiteralPath (Join-Path $taskOutput 'metrics.txt')
Write-Output "Capture saved: $taskOutput"
