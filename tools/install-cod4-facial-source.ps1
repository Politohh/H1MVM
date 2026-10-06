param(
    [Parameter(Mandatory = $true)][string]$RawDirectory,
    [string]$GameDirectory = 'C:\Program Files (x86)\Steam\steamapps\common\Call of Duty Modern Warfare Remastered',
    [string]$BackupRoot = 'F:\Dev\h1-releases\install-backups'
)
$ErrorActionPreference = 'Stop'
if (Get-Process h1-mod -ErrorAction SilentlyContinue) { throw 'Close H1 before installing facial conversion inputs.' }
$targetRoot = [IO.Path]::GetFullPath((Join-Path $GameDirectory 'cod4_facial'))
$backup = Join-Path $BackupRoot ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-before-facial-source')
$files = @()
foreach ($category in @('xanim', 'xmodelparts')) {
    $directory = Join-Path $RawDirectory $category
    foreach ($source in Get-ChildItem -LiteralPath $directory -File) {
        if ($source.Name -notmatch '^[a-zA-Z0-9_]+$') { throw 'Unexpected source asset filename.' }
        $relative = Join-Path $category $source.Name
        $target = [IO.Path]::GetFullPath((Join-Path $targetRoot $relative))
        if (!$target.StartsWith($targetRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Facial target escaped install directory.' }
        $files += @{ Source = $source.FullName; Target = $target; Relative = $relative;
            NewHash = (Get-FileHash -LiteralPath $source.FullName).Hash; Existed = (Test-Path -LiteralPath $target -PathType Leaf) }
    }
}
if (!$files.Count) { throw 'No source assets supplied.' }
New-Item -ItemType Directory -Path $backup | Out-Null
foreach ($file in $files) {
    if ($file.Existed) {
        $file.OldHash = (Get-FileHash -LiteralPath $file.Target).Hash
        $saved = Join-Path $backup $file.Relative
        New-Item -ItemType Directory -Path (Split-Path $saved -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $file.Target -Destination $saved
        if ((Get-FileHash -LiteralPath $saved).Hash -ne $file.OldHash) { throw 'Facial rollback hash mismatch.' }
    }
}
try {
    foreach ($file in $files) {
        New-Item -ItemType Directory -Path (Split-Path $file.Target -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $file.Source -Destination $file.Target -Force
        if ((Get-FileHash -LiteralPath $file.Target).Hash -ne $file.NewHash) { throw 'Facial install hash mismatch.' }
    }
} catch {
    foreach ($file in $files) {
        if ($file.Existed) { Copy-Item -LiteralPath (Join-Path $backup $file.Relative) -Destination $file.Target -Force }
        elseif (Test-Path -LiteralPath $file.Target -PathType Leaf) { Remove-Item -LiteralPath $file.Target }
    }
    throw
}
@{ InstalledAt = (Get-Date).ToString('o'); TargetRoot = $targetRoot; BackupDirectory = $backup; Files = $files } |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $backup 'manifest.json') -Encoding utf8
Write-Output "Installed $($files.Count) local Mod Tools conversion inputs with SHA-256 parity. Backup: $backup"
