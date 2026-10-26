param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw 'vswhere.exe не найден: Visual Studio не установлена'
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    throw 'не найден установленный компонент MSVC (C++ build tools)'
}

$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) {
    throw "не найден $vcvars"
}

Write-Output "MSVC: $vsPath"

$apiSources = @{
    'ClientEcho.cpp' = 'ClientApi\ClientApi.cpp'
    'ClientTime.cpp' = 'ClientApi\ClientApi.cpp'
    'ClientRand.cpp' = 'ClientApi\ClientApi.cpp'
    'RConsole.cpp'   = 'ConsoleApi\ConsoleApi.cpp'
}

$skipDirectories = @('ClientApi', 'ConsoleApi')

$total = 0

foreach ($number in 2..10) {
    $lab = "lab$number"
    $labPath = Join-Path $Root $lab

    if (-not (Test-Path $labPath)) {
        continue
    }

    $buildDir = Join-Path $labPath 'build'
    New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

    $sources = Get-ChildItem -Path $labPath -Filter *.cpp -Recurse |
        Where-Object { $skipDirectories -notcontains $_.Directory.Name }

    foreach ($source in $sources) {
        $arguments = "/EHsc /utf-8 /W3 `"$($source.FullName)`""

        if ($apiSources.ContainsKey($source.Name)) {
            $apiPath = Join-Path $labPath $apiSources[$source.Name]
            $arguments += " `"$apiPath`""
        }

        $outputName = [System.IO.Path]::GetFileNameWithoutExtension($source.Name)

        if ($source.Name -eq 'ServiceLibrary.cpp') {
            $arguments += " /LD /Fe:`"$(Join-Path $buildDir 'ServiceLibrary.dll')`""
        }
        else {
            $arguments += " /Fe:`"$(Join-Path $buildDir "$outputName.exe")`""
        }

        $command = "call `"$vcvars`" >nul && cl /nologo $arguments"

        Write-Output "  $lab / $($source.Name)"
        & cmd.exe /c $command

        if ($LASTEXITCODE -ne 0) {
            throw "ошибка сборки: $lab / $($source.Name)"
        }

        $total++
    }
}

Write-Output "Собрано файлов: $total"
