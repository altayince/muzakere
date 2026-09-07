param(
    [ValidateSet('Setup','Build','Test','Run')][string]$Action = 'Build'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    $python = Join-Path $projectRoot '.tools\python\Scripts\python.exe'
    $qtRoot = Join-Path $projectRoot '.tools\Qt\6.8.3\mingw_64'
    $mingwBin = Join-Path $projectRoot '.tools\Qt\Tools\mingw1310_64\bin'
    function Invoke-Checked([string]$Executable, [string[]]$Arguments) {
        & $Executable @Arguments
        if ($LASTEXITCODE -ne 0) { throw "$Executable failed with exit code $LASTEXITCODE" }
    }
    if ($Action -eq 'Setup') {
        if (!(Test-Path -LiteralPath $python)) { Invoke-Checked 'python' @('-m','venv','.tools\python') }
        Invoke-Checked $python @('-m','pip','install','--disable-pip-version-check','aqtinstall==3.3.0','cmake==4.4.3','ninja==1.13.2','pypdfium2==5.13.0')
        Invoke-Checked $python @('scripts/prepare-pdfium.py')
        if (!(Test-Path -LiteralPath "$qtRoot\bin\qmake.exe")) {
            Invoke-Checked $python @('-m','aqt','install-qt','windows','desktop','6.8.3','win64_mingw','--archives','qtbase','--outputdir','.tools\Qt')
        }
        if (!(Test-Path -LiteralPath "$mingwBin\g++.exe")) {
            Invoke-Checked $python @('-m','aqt','install-tool','windows','desktop','tools_mingw1310','qt.tools.win64_mingw1310','--outputdir','.tools\Qt')
        }
        Write-Host 'Toolchain ready. Run scripts/dev.ps1 Build, then Test or Run.'
        return
    }
    if (!(Test-Path -LiteralPath "$mingwBin\g++.exe")) { throw 'Run scripts/dev.ps1 Setup first.' }
    $env:PATH = "$qtRoot\bin;$mingwBin;$projectRoot\.tools\python\Scripts;$env:PATH"
    $env:QT_PLUGIN_PATH = "$qtRoot\plugins"
    if ($Action -eq 'Build') {
        Invoke-Checked 'cmake' @('--preset','dev',"-DCMAKE_PREFIX_PATH=$qtRoot")
        Invoke-Checked 'cmake' @('--build','--preset','dev','--parallel','4')
    } elseif ($Action -eq 'Test') {
        Invoke-Checked 'ctest' @('--preset','dev')
    } else {
        Invoke-Checked "$projectRoot\build\dev\muzakere.exe" @()
    }
} finally { Pop-Location }
