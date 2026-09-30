param([string]$Compiler = 'D:\Apps\OpenMV IDE 5.0.0\share\qtcreator\stedgeai\Utilities\windows\mingw64\bin\gcc.exe')
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$testDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('ldr-iot-test-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $testDirectory)
$previousPath = $env:PATH
try {
    $env:PATH = (Split-Path -Parent $Compiler) + [IO.Path]::PathSeparator + $previousPath
    $executable = Join-Path $testDirectory 'test_iot.exe'
    $sources = @('Hardware/button.c','Hardware/buzzer.c','Hardware/DS1302.c','Hardware/ESP8266.c',
                 'Software/RtcCalendar.c','Software/IotProtocol.c','Software/MorseMessage.c',
                 'Software/MessageService.c','Software/LdrReceiver.c','Software/MorseDecoder.c','Tests/test_iot.c')
    $arguments = @('-std=c99','-Wall','-Wextra','-Werror','-pedantic','-O2',
                   '-I', (Join-Path $PSScriptRoot 'iot_stubs'),
                   '-I', (Join-Path $projectDirectory 'Software'), '-I', (Join-Path $projectDirectory 'Hardware'))
    $arguments += $sources | ForEach-Object { Join-Path $projectDirectory $_ }
    $arguments += @('-o', $executable)
    & $Compiler @arguments
    if ($LASTEXITCODE -ne 0) { throw "IoT test build failed: $LASTEXITCODE" }
    & $executable
    if ($LASTEXITCODE -ne 0) { throw "IoT tests failed: $LASTEXITCODE" }
} finally {
    $env:PATH = $previousPath
    $resolved = [IO.Path]::GetFullPath($testDirectory)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe cleanup path' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
