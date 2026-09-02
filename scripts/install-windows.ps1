param(
    [switch]$NoAutostart,
    [switch]$Windowed
)

$ErrorActionPreference = 'Stop'

$running = Get-Process -Name 'zen-writer' -ErrorAction SilentlyContinue
if ($running) {
    throw 'Close Zen Writer before installing an update.'
}

$sourceDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$targetDir = Join-Path $env:LOCALAPPDATA 'ZenWriter'
New-Item -ItemType Directory -Path $targetDir -Force | Out-Null

Get-ChildItem -LiteralPath $sourceDir -Force | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $targetDir -Recurse -Force
}

$executable = Join-Path $targetDir 'zen-writer.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    throw "zen-writer.exe was not found in $targetDir"
}

if (-not $NoAutostart) {
    $startupDir = [Environment]::GetFolderPath('Startup')
    $shortcutPath = Join-Path $startupDir 'Zen Writer.lnk'
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = $executable
    $shortcut.Arguments = '--fullscreen'
    $shortcut.WorkingDirectory = $targetDir
    $shortcut.Save()
}

$arguments = if ($Windowed) { '--windowed' } else { '--fullscreen' }
Start-Process -FilePath $executable -ArgumentList $arguments
