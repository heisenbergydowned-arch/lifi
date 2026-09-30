param([string]$Compiler = 'D:\Apps\OpenMV IDE 5.0.0\share\qtcreator\stedgeai\Utilities\windows\mingw64\bin\gcc.exe')
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$testDirectory = Join-Path ([IO.Path]::GetTempPath()) ('morse-gateway-test-' + [guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $testDirectory)
$previousPath = $env:PATH
try {
    $compilerDirectory = Split-Path -Parent $Compiler
    $env:PATH = $compilerDirectory + [IO.Path]::PathSeparator + $previousPath
    $common = @('-Wall','-Wextra','-Werror','-pedantic','-O2','-I', (Join-Path $projectDirectory 'Software'), '-I', (Join-Path $projectDirectory 'Hardware'))
    $objects = @()
    foreach ($source in @('IotProtocol','RtcCalendar','ProtocolConfig')) {
        $object = Join-Path $testDirectory ($source + '.o')
        & $Compiler @common '-std=c99' '-c' (Join-Path $projectDirectory ('Software/' + $source + '.c')) '-o' $object
        if ($LASTEXITCODE -ne 0) { throw 'Shared codec build failed' }
        $objects += $object
    }
    $executable = Join-Path $testDirectory 'gateway.exe'
    $arguments = $common + @('-std=c++17','-fno-exceptions','-fno-rtti','-fno-use-cxa-atexit','-I', (Join-Path $PSScriptRoot 'gateway_stubs'),
                              '-I', (Join-Path $projectDirectory 'ESP8266_Gateway/include'),
                              '-include', (Join-Path $PSScriptRoot 'gateway_stubs/BuildCheckConfig.h'),
                              '-c', (Join-Path $PSScriptRoot 'test_gateway.cpp'), '-o', (Join-Path $testDirectory 'gateway.o'))
    & $Compiler @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Gateway host build failed' }
    $objects += Join-Path $testDirectory 'gateway.o'
    & $Compiler @objects '-o' $executable
    if ($LASTEXITCODE -ne 0) { throw 'Gateway host link failed' }
    & $executable
    if ($LASTEXITCODE -ne 0) { throw 'Gateway tests failed' }
} finally {
    $env:PATH = $previousPath
    $resolved = [IO.Path]::GetFullPath($testDirectory)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe cleanup path' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
