param(
    [string]$GameDirectory = 'C:\Program Files (x86)\Steam\steamapps\common\Call of Duty Modern Warfare Remastered',
    [string]$DataDirectory = "$env:LOCALAPPDATA\h1-mod\cdata",
    [string]$BackupRoot = 'F:\Dev\h1-releases\install-backups',
    [string]$SkySourceDirectory = 'F:\Dev\h1-theater-skies',
    [string]$SniperSourceDirectory = 'F:\Dev\h1-sniper-source',
    [string]$ScopeImage = '',
    [switch]$SkipOptionalAssets
)
$ErrorActionPreference = 'Stop'
if (Get-Process h1-mod -ErrorAction SilentlyContinue) { throw 'Close H1 before installing.' }
$repository = Split-Path $PSScriptRoot -Parent
$build = Join-Path $repository 'build\bin\x64\Release'
$backup = Join-Path $BackupRoot ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-before-native-theater')
$files = @(
    @{ Source = Join-Path $build 'h1-mod.exe'; Target = Join-Path $GameDirectory 'h1-mod.exe'; Backup = 'h1-mod.exe' },
    @{ Source = Join-Path $build 'h1-mod.pdb'; Target = Join-Path $GameDirectory 'h1-mod.pdb'; Backup = 'h1-mod.pdb' },
    @{ Source = Join-Path $repository 'data\cdata\ui_scripts\theater\__init__.lua'; Target = Join-Path $DataDirectory 'ui_scripts\theater\__init__.lua'; Backup = 'theater\__init__.lua' },
    @{ Source = Join-Path $repository 'data\cdata\ui_scripts\theater\timeline.lua'; Target = Join-Path $DataDirectory 'ui_scripts\theater\timeline.lua'; Backup = 'theater\timeline.lua' },
    @{ Source = Join-Path $repository 'data\cdata\ui_scripts\theater\editor.lua'; Target = Join-Path $DataDirectory 'ui_scripts\theater\editor.lua'; Backup = 'theater\editor.lua' },
    @{ Source = Join-Path $repository 'data\cdata\ui_scripts\theater\library.lua'; Target = Join-Path $DataDirectory 'ui_scripts\theater\library.lua'; Backup = 'theater\library.lua' },
    @{ Source = Join-Path $repository 'data\cdata\ui_scripts\theater\match_hud.lua'; Target = Join-Path $DataDirectory 'ui_scripts\theater\match_hud.lua'; Backup = 'theater\match_hud.lua' },
    @{ Source = Join-Path $repository 'data\cdata\ui_scripts\theater\pause_menu.lua'; Target = Join-Path $DataDirectory 'ui_scripts\theater\pause_menu.lua'; Backup = 'theater\pause_menu.lua' },
    @{ Source = Join-Path $repository 'data\cdata\ui_scripts\patches\pausequit.lua'; Target = Join-Path $DataDirectory 'ui_scripts\patches\pausequit.lua'; Backup = 'patches\pausequit.lua' },
    @{ Source = Join-Path $repository 'data\cdata\ui_scripts\server_list\lobby.lua'; Target = Join-Path $DataDirectory 'ui_scripts\server_list\lobby.lua'; Backup = 'server_list\lobby.lua' }
)
$depotScripts = @('__init__.lua', 'inventory.lua', 'depot_override.lua', 'scoreboard_override.lua')
foreach ($script in $depotScripts) {
    $files += @{ Source = Join-Path $repository "data\cdata\ui_scripts\custom_depot\$script"; Target = Join-Path $DataDirectory "ui_scripts\custom_depot\$script"; Backup = "custom_depot\$script" }
}
$skyManifest = Join-Path $SkySourceDirectory 'manifest.json'
$sniperManifest = Join-Path $SniperSourceDirectory 'manifest.json'
if (!$SkipOptionalAssets -and (Test-Path -LiteralPath $sniperManifest -PathType Leaf)) {
    $sniperInputs = Get-Content -LiteralPath $sniperManifest -Raw | ConvertFrom-Json
    foreach ($inputFile in $sniperInputs.files) {
        if ($inputFile.name -notmatch '^(viewmodel_[a-z0-9_]+|view(model_[a-z0-9_]+|hands_usmc3)\.rig|animations\.json|scope\.png)$') { throw 'Unexpected sniper asset path' }
        $source = Join-Path $SniperSourceDirectory $inputFile.name
        if ((Get-FileHash -LiteralPath $source).Hash -ne $inputFile.sha256) { throw 'Sniper source provenance mismatch' }
        if ($inputFile.name -eq 'scope.png') {
            # Keep the authenticated original intact; install a local custom
            # scope verbatim and record its own source/hash in the manifest.
            $customScope = if ($ScopeImage) { $ScopeImage } else { Join-Path $SniperSourceDirectory 'scope_upscale.png' }
            if ($ScopeImage -and !(Test-Path -LiteralPath $customScope -PathType Leaf)) { throw 'Missing custom scope image' }
            if (Test-Path -LiteralPath $customScope -PathType Leaf) { $source = $customScope }
        }
        $files += @{ Source=$source; Target=(Join-Path $GameDirectory "cod4_snipers\$($inputFile.name)"); Backup="cod4_snipers\$($inputFile.name)" }
    }
}
if (!$SkipOptionalAssets -and (Test-Path -LiteralPath $skyManifest -PathType Leaf)) {
    $skies = Get-Content -LiteralPath $skyManifest -Raw | ConvertFrom-Json
    foreach ($sky in $skies.assets) {
        if ($sky.name -notmatch '^[a-zA-Z0-9_]+$' -or $sky.zone -notmatch '^[a-zA-Z0-9_]+$') { throw 'Invalid sky manifest path' }
        $source = Join-Path $SkySourceDirectory "$($sky.zone)\$($sky.name).h1sky"
        if (!(Test-Path -LiteralPath $source -PathType Leaf) -or (Get-FileHash -LiteralPath $source).Hash -ne $sky.sha256) { throw "Sky provenance mismatch: $($sky.name)" }
        $files += @{ Source=$source; Target=(Join-Path $GameDirectory "demos\theater\skies\$($sky.name).h1sky"); Backup="skies\$($sky.name).h1sky" }
    }
}
foreach ($file in $files) {
    if (!(Test-Path -LiteralPath $file.Source -PathType Leaf)) { throw "Missing build input: $($file.Source)" }
    $file.NewHash = (Get-FileHash -LiteralPath $file.Source -Algorithm SHA256).Hash
    $file.Existed = Test-Path -LiteralPath $file.Target -PathType Leaf
}
New-Item -ItemType Directory -Path $backup | Out-Null
foreach ($file in $files) {
    if ($file.Existed) {
        $file.OldHash = (Get-FileHash -LiteralPath $file.Target -Algorithm SHA256).Hash
        $saved = Join-Path $backup $file.Backup
        New-Item -ItemType Directory -Path (Split-Path $saved -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $file.Target -Destination $saved
        if ((Get-FileHash -LiteralPath $saved).Hash -ne $file.OldHash) { throw 'Rollback hash mismatch' }
    }
}
try {
    foreach ($file in $files) {
        New-Item -ItemType Directory -Path (Split-Path $file.Target -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $file.Source -Destination $file.Target -Force
        if ((Get-FileHash -LiteralPath $file.Target).Hash -ne $file.NewHash) { throw "Install hash mismatch: $($file.Target)" }
    }
} catch {
    foreach ($file in $files) {
        if ($file.Existed) { Copy-Item -LiteralPath (Join-Path $backup $file.Backup) -Destination $file.Target -Force }
        elseif (Test-Path -LiteralPath $file.Target -PathType Leaf) { Remove-Item -LiteralPath $file.Target }
    }
    throw
}
$manifest = [ordered]@{
    BaseCommit = (git -C $repository rev-parse HEAD).Trim()
    InstalledAt = (Get-Date).ToString('o')
    BackupDirectory = $backup
    Files = $files
    Runtime = 'Pending H1 playback validation'
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $backup 'manifest.json') -Encoding utf8
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $repository 'build\native-theater-install.json') -Encoding utf8
Write-Output "Installed $($files.Count) files with SHA-256 parity. Rollback: $backup"
