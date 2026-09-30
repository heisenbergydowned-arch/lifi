param([string]$Compiler = '', [switch]$AdcDiagnostics)

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

# Test production receiver and decoder with only the acquisition API stubbed.
# Build outside the firmware project; leave no executable or object in source.
$testDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('ldr-receiver-test-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $testDirectory)
$previousPath = $env:PATH
try {
    $env:PATH = (Split-Path -Parent $Compiler) + [System.IO.Path]::PathSeparator + $previousPath
    foreach ($dotMs in @(60, 240)) {
        foreach ($brightHigh in @(0, 1)) {
            $executable = Join-Path $testDirectory ("test_ldr_receiver_${dotMs}_${brightHigh}.exe")
            $compileArguments = @(
                '-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O2',
                '-D__USE_MINGW_ANSI_STDIO=0', '-Dprintf=receiver_test_printf',
                "-DLDR_MORSE_DOT_MS=$dotMs", "-DLDR_BRIGHT_IS_HIGH=$brightHigh",
                '-I', (Join-Path $PSScriptRoot 'receiver_stubs'),
                '-I', (Join-Path $projectDirectory 'Software'),
                '-I', (Join-Path $projectDirectory 'Hardware'),
                (Join-Path $PSScriptRoot 'test_ldr_receiver.c'),
                (Join-Path $projectDirectory 'Software\LdrReceiver.c'),
                (Join-Path $projectDirectory 'Software\MorseDecoder.c'),
                '-o', $executable
            )
            if ($AdcDiagnostics) { $compileArguments += '-DLDR_DEBUG_ADC=1' }
            & $Compiler @compileArguments
            if ($LASTEXITCODE -ne 0) { throw "Receiver C compilation failed: dot=$dotMs, polarity=$brightHigh, exit=$LASTEXITCODE" }
            & $executable
            if ($LASTEXITCODE -ne 0) { throw "Receiver tests failed: dot=$dotMs, polarity=$brightHigh, exit=$LASTEXITCODE" }
        }
    }
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
