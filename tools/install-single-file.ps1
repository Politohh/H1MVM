param(
    [string]$GameDirectory = 'C:\Program Files (x86)\Steam\steamapps\common\Call of Duty Modern Warfare Remastered',
    [string]$Executable = '',
    [string]$BackupRoot = 'F:\Dev\Rollback Backups'
)
$ErrorActionPreference = 'Stop'
if (Get-Process h1-mod, h1_mp64_ship, h1_sp64_ship -ErrorAction SilentlyContinue) {
    throw 'Close H1 before installing.'
}
if (!$Executable) {
    $Executable = Join-Path (Split-Path $PSScriptRoot -Parent) 'build\bin\x64\Release\h1-mod.exe'
}
if (!(Test-Path -LiteralPath $Executable -PathType Leaf)) { throw 'Missing release EXE' }
if (!(Test-Path -LiteralPath (Join-Path $GameDirectory 'h1_mp64_ship.exe'))) { throw 'Not an MWR game directory' }
$target = Join-Path $GameDirectory 'h1-mod.exe'
$backup = Join-Path $BackupRoot ((Get-Date -Format 'yyyyMMdd-HHmmss') + '-before-single-file-h1')
New-Item -ItemType Directory -Path $backup | Out-Null
$before = $null
if (Test-Path -LiteralPath $target) {
    $before = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
    Copy-Item -LiteralPath $target -Destination (Join-Path $backup 'h1-mod.exe')
    if ((Get-FileHash -LiteralPath (Join-Path $backup 'h1-mod.exe')).Hash -ne $before) { throw 'Rollback verification failed' }
}
$expected = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash
Copy-Item -LiteralPath $Executable -Destination $target -Force
if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $expected) { throw 'Installed EXE verification failed' }
@{ source=$Executable; target=$target; sha256=$expected; previous_sha256=$before; backup=$backup } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'installation.json') -Encoding UTF8
Write-Output "Installed verified single-file EXE: $expected"
Write-Output "Rollback: $backup"
