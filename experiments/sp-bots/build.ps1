param([string]$PackageDirectory = 'build/sp-bots-preview')
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$package = [IO.Path]::GetFullPath((Join-Path $repository $PackageDirectory))
if (!$package.StartsWith($repository + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Package output must be inside this checkout.'
}
if (Test-Path -LiteralPath $package) { throw 'Choose a fresh -PackageDirectory; existing previews are preserved.' }
if ($env:AURORAH1_GAME_PATH) { throw 'Unset AURORAH1_GAME_PATH and regenerate first; this build must not auto-install.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
if (!$installation) { throw 'Visual Studio C++ build tools were not found.' }
$msbuild = Join-Path $installation 'MSBuild/Current/Bin/MSBuild.exe'
$native = Join-Path $repository 'build/sp-bots/native'
$objects = Join-Path $repository 'build/sp-bots/obj'
$libraries = Join-Path $repository 'build/bin/x64/Release'
if (!(Test-Path -LiteralPath (Join-Path $libraries 'common.lib'))) {
    throw 'Build the normal Release/x64 solution and configure its runtime bundle first. See README.md.'
}
New-Item -ItemType Directory -Force -Path $native,$objects | Out-Null
Get-ChildItem -LiteralPath $libraries -Filter '*.lib' | Where-Object Name -ne 'h1-mod.lib' | Copy-Item -Destination $native
Push-Location $repository
try {
    & $msbuild build/client.vcxproj /m /p:Configuration=Release /p:Platform=x64 /p:BuildProjectReferences=false "/p:OutDir=$native\" "/p:IntDir=$objects\" "/p:ForceImportBeforeCppTargets=$PSScriptRoot/spai.props" /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw 'SP Bots native build failed.' }
    $joined = @('spai_entry.gsc','spai_match.gsc','spai_behavior.gsc') | ForEach-Object {
        [IO.File]::ReadAllText((Join-Path $PSScriptRoot $_)).Replace("`r`n", "`n")
    }
    $script = Join-Path $native 'spai.gsc'
    [IO.File]::WriteAllText($script, ($joined -join "`n"), [Text.UTF8Encoding]::new($false))
    & python "$PSScriptRoot/package.py" --source "$native/h1-mod.exe" --script $script --destination $package --ui "$PSScriptRoot/spai_menu.lua" --startup-ui "$PSScriptRoot/startup_lobby.lua"
    if ($LASTEXITCODE -ne 0) { throw 'SP Bots bundle validation failed.' }
    Copy-Item -LiteralPath "$PSScriptRoot/Launch-SP-Bots.cmd" -Destination $package
    Write-Host "WIP preview ready at $package. No game files were replaced."
} finally { Pop-Location }
