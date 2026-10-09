# Rebuild the already configured Windows installation on this laptop.
# Run from PowerShell: .\scripts\rebuild-local.ps1
$ErrorActionPreference = 'Stop'
$pythonCommand = (Get-Command python -ErrorAction Stop).Source
$root = Split-Path $PSScriptRoot -Parent
$launcher = Join-Path $root 'data/local.ps1'
if (!(Test-Path $launcher)) { throw 'Set up the local installation first; see docs/WINDOWS.md.' }
if (!(Test-Path (Join-Path $root 'build/CMakeCache.txt'))) { throw 'Configure CMake first; see docs/WINDOWS.md.' }
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
$env:BOT_TZDATA_DIR = Join-Path $root 'build/tzdata'
& $launcher stop
Start-Sleep -Seconds 1
# SQLite Online Backup also captures committed WAL contents.
$database = Join-Path $root 'data/local.sqlite3'
if (Test-Path $database) {
    $backup = Join-Path $root ('data/before-build-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.sqlite3')
    @'
import sqlite3,sys
source=sqlite3.connect(sys.argv[1])
target=sqlite3.connect(sys.argv[2])
source.backup(target)
assert target.execute('PRAGMA integrity_check').fetchone()[0]=='ok'
target.close()
source.close()
'@ | & $pythonCommand - $database $backup
    if ($LASTEXITCODE -ne 0) { throw 'Database backup failed. Bot remains stopped.' }
    Write-Output "Database backup: $backup"
}
& cmake --build (Join-Path $root 'build') -j2
if ($LASTEXITCODE -ne 0) { throw 'Build failed. Fix the error before starting the bot.' }
& ctest --test-dir (Join-Path $root 'build') --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Tests failed. Bot remains stopped.' }
& $launcher start
& $launcher status
