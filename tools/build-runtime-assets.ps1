$ErrorActionPreference = 'Stop'
$repository = Split-Path $PSScriptRoot -Parent
$configuration = Join-Path $repository 'build/runtime-assets-inputs.json'
if (!(Test-Path -LiteralPath $configuration)) {
    throw 'Configure the complete runtime bundle with tools/pack-runtime-assets.py --configure inputs.json. See docs/single-file-release.md.'
}
$inputs = Get-Content -LiteralPath $configuration -Raw | ConvertFrom-Json
& $inputs.python (Join-Path $PSScriptRoot 'pack-runtime-assets.py')
if ($LASTEXITCODE -ne 0) { throw 'Runtime bundle validation failed' }
