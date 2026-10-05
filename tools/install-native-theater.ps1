param(
    [string]$GameDirectory = 'C:\Program Files (x86)\Steam\steamapps\common\Call of Duty Modern Warfare Remastered',
    [string]$DataDirectory = "$env:LOCALAPPDATA\h1-mod\cdata",
    [string]$BackupRoot = 'F:\Dev\h1-releases\install-backups'
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
    @{ Source = Join-Path $repository 'data\cdata\ui_scripts\theater\editor.lua'; Target = Join-Path $DataDirectory 'ui_scripts\theater\editor.lua'; Backup = 'theater\editor.lua' }
)
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
