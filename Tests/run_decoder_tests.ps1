param([string]$Compiler = '')

$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
if (-not $Compiler) {
    $candidate = Get-Command gcc, clang -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($candidate) {
        $Compiler = $candidate.Source
    } else {
        $bundledGcc = 'D:\Apps\OpenMV IDE 5.0.0\share\qtcreator\stedgeai\Utilities\windows\mingw64\bin\gcc.exe'
        if (Test-Path -LiteralPath $bundledGcc) { $Compiler = $bundledGcc }
    }
}
if (-not $Compiler -or -not (Test-Path -LiteralPath $Compiler)) {
    throw 'Host GCC/Clang not found. Use -Compiler with the full path to gcc.exe or clang.exe.'
}

# Build outside the firmware project; leave no executable or object in source.
$testDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('morse-decoder-test-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $testDirectory)
$executable = Join-Path $testDirectory 'test_morse_decoder.exe'
$previousPath = $env:PATH
try {
    $env:PATH = (Split-Path -Parent $Compiler) + [System.IO.Path]::PathSeparator + $previousPath
    $compileArguments = @(
        '-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O2',
        '-I', (Join-Path $projectDirectory 'Software'),
        (Join-Path $projectDirectory 'Software\MorseDecoder.c'),
        (Join-Path $PSScriptRoot 'test_morse_decoder.c'),
        '-o', $executable
    )
    & $Compiler @compileArguments
    if ($LASTEXITCODE -ne 0) { throw "C compilation failed: $LASTEXITCODE" }
    & $executable
    if ($LASTEXITCODE -ne 0) { throw "Decoder tests failed: $LASTEXITCODE" }
} finally {
    $env:PATH = $previousPath
    $resolvedTestDirectory = [System.IO.Path]::GetFullPath($testDirectory)
    $resolvedTempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolvedTestDirectory.StartsWith($resolvedTempRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing cleanup outside the temporary directory.'
    }
    if (Test-Path -LiteralPath $resolvedTestDirectory) {
        Remove-Item -LiteralPath $resolvedTestDirectory -Recurse -Force
    }
}
