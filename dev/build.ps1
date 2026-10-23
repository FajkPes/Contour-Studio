param([switch]$CoreOnly, [switch]$Run, [switch]$Publish)
$ErrorActionPreference = 'Stop'
$taskRoot = $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Install Visual Studio with Desktop development with C++.' }
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (!(Test-Path $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
$arguments = @('--fresh', '-S', $taskRoot, '-B', (Join-Path $taskRoot 'build'), '-G', 'Visual Studio 18 2026', '-A', 'x64,version=10.0.26100.0', '-DCMAKE_SYSTEM_VERSION=10.0.26100.0')
if (!$CoreOnly) {
    $cache = Join-Path $env:USERPROFILE '.nuget\packages'
    function Find-Package([string]$name) {
        $directory = Join-Path $cache $name
        $item = Get-ChildItem -LiteralPath $directory -Directory -ErrorAction SilentlyContinue | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
        if (!$item) { throw "Missing $name. Restore the Windows App SDK dependencies with: dotnet restore dependencies\Dependencies.csproj" }
        return $item.FullName
    }
    $winui = Find-Package 'microsoft.windowsappsdk.winui'
    $foundation = Find-Package 'microsoft.windowsappsdk.foundation'
    $interactive = Find-Package 'microsoft.windowsappsdk.interactiveexperiences'
    $webview = Find-Package 'microsoft.web.webview2'
    $sdkBase = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
    $sdkVersion = Get-Item (Join-Path $sdkBase 'bin\10.0.26100.0')
    $generator = Join-Path $sdkVersion.FullName 'x64\cppwinrt.exe'
    $headers = Join-Path $taskRoot 'generated'
    if (!(Test-Path (Join-Path $headers 'winrt\Microsoft.UI.Xaml.h'))) {
        $metadata = Join-Path $headers 'metadata'
        New-Item -ItemType Directory -Path $metadata -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $sdkBase "UnionMetadata\$($sdkVersion.Name)\Windows.winmd") -Destination $metadata
        foreach ($source in @((Join-Path $winui 'metadata'), (Join-Path $interactive 'metadata\10.0.17763.0'), (Join-Path $foundation 'metadata'))) { Get-ChildItem -LiteralPath $source -Filter '*.winmd' | Copy-Item -Destination $metadata }
        Get-ChildItem -LiteralPath $webview -Filter '*.winmd' -Recurse | Select-Object -First 1 | Copy-Item -Destination $metadata
        $generationArguments = @('-input', $metadata, '-output', $headers)
        & $generator @generationArguments
        if ($LASTEXITCODE) { throw 'C++/WinRT header generation failed.' }
    }
    $arguments += @('-DBUILD_WINUI=ON', "-DWINRT_HEADERS=$headers", "-DWINUI_PACKAGE=$winui", "-DFOUNDATION_PACKAGE=$foundation")
} else { $arguments += '-DBUILD_WINUI=OFF' }
& $cmake @arguments
if ($LASTEXITCODE) { throw 'CMake configure failed.' }
& $cmake --build (Join-Path $taskRoot 'build') --config Release --parallel 1 -- /nr:false
if ($LASTEXITCODE) { throw 'Compilation failed.' }
& (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir (Join-Path $taskRoot 'build') -C Release --output-on-failure
if ($LASTEXITCODE) { throw 'Tests failed.' }
if ($Publish -and !$CoreOnly) {
    $version = (Get-Content -LiteralPath (Join-Path $taskRoot 'version.txt') -Raw).Trim()
    if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid release version' }
    $releaseRoot = Join-Path (Split-Path $taskRoot) 'release'
    $release = Join-Path $releaseRoot $version
    New-Item -ItemType Directory -Path $release -Force | Out-Null
    foreach ($file in @('ContourStudio.exe', 'Microsoft.WindowsAppRuntime.Bootstrap.dll')) { Copy-Item -LiteralPath (Join-Path $taskRoot "build\Release\$file") -Destination $release -Force }
    Copy-Item -LiteralPath (Join-Path $taskRoot 'RELEASE-README.md') -Destination (Join-Path $release 'README.md') -Force
    Set-Content -LiteralPath (Join-Path $releaseRoot 'latest.txt') -Value $version -Encoding Ascii
    Get-ChildItem -LiteralPath $release -File | Where-Object Name -ne 'SHA256.txt' | Get-FileHash -Algorithm SHA256 | ForEach-Object { "$($_.Hash)  $([IO.Path]::GetFileName($_.Path))" } | Set-Content -LiteralPath (Join-Path $release 'SHA256.txt') -Encoding Ascii
    Write-Output "Release ready: $release"
}
if ($Run -and !$CoreOnly) { Start-Process -FilePath (Join-Path $taskRoot 'build\Release\ContourStudio.exe') }
