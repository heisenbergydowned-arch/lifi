param([string]$Compiler = 'D:\Apps\OpenMV IDE 5.0.0\share\qtcreator\stedgeai\Utilities\windows\mingw64\bin\gcc.exe')
$ErrorActionPreference = 'Stop'
foreach ($script in @('run_decoder_tests.ps1','run_adc_tests.ps1','run_receiver_tests.ps1','run_iot_tests.ps1','run_gateway_tests.ps1','run_display_tests.ps1')) {
    & (Join-Path $PSScriptRoot $script) -Compiler $Compiler
    if (-not $?) { throw ('Failed: ' + $script) }
}
