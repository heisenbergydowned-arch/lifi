param([string]$Compiler = 'D:\Apps\OpenMV IDE 5.0.0\share\qtcreator\stedgeai\Utilities\windows\mingw64\bin\gcc.exe')
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$testDirectory = Join-Path ([IO.Path]::GetTempPath()) ('ldr-display-test-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $testDirectory)
$previousPath = $env:PATH
try {
    $env:PATH = (Split-Path -Parent $Compiler) + [IO.Path]::PathSeparator + $previousPath
    $executable = Join-Path $testDirectory 'test_display.exe'
    $arguments = @('-std=c99','-Wall','-Wextra','-Werror','-pedantic','-O2')
    foreach ($folder in @('Software','Hardware','OLED')) { $arguments += @('-I', (Join-Path $projectDirectory $folder)) }
    $arguments += @((Join-Path $projectDirectory 'Software\ReceiverDisplay.c'), (Join-Path $PSScriptRoot 'test_display.c'), '-o', $executable)
    & $Compiler @arguments
    if ($LASTEXITCODE -ne 0) { throw "Display test build failed: $LASTEXITCODE" }
    & $executable
    if ($LASTEXITCODE -ne 0) { throw "Display tests failed: $LASTEXITCODE" }
} finally {
    $env:PATH = $previousPath
    $resolved = [IO.Path]::GetFullPath($testDirectory)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe cleanup path' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
