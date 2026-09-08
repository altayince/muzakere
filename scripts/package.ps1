param([string]$ProfileFile='')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    $qtRoot=Join-Path $projectRoot '.tools/Qt/6.8.3/mingw_64'
    $mingw=Join-Path $projectRoot '.tools/Qt/Tools/mingw1310_64/bin'
    $env:PATH="$qtRoot\bin;$mingw;$projectRoot\.tools\python\Scripts;$env:PATH"
    $compiler=Join-Path $projectRoot '.tools/InnoSetup/ISCC.exe'
    if (!(Test-Path -LiteralPath $compiler)) { throw 'Run scripts/setup-installer-tools.ps1 first.' }
    function Invoke-Checked([string]$File,[string[]]$Arguments) {
        & $File @Arguments
        if ($LASTEXITCODE -ne 0) { throw "$File failed: $LASTEXITCODE" }
    }
    Invoke-Checked 'cmake' @('-S','.','-B','build/release','-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=OFF',"-DCMAKE_PREFIX_PATH=$qtRoot")
    Invoke-Checked 'cmake' @('--build','build/release','--parallel','4')
    $stage=Join-Path $projectRoot ('out/package-'+[guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage | Out-Null
    Copy-Item -LiteralPath 'build/release/muzakere.exe','build/release/muz_pdf_worker.exe','build/release/pdfium.dll' -Destination $stage
    Invoke-Checked "$qtRoot/bin/windeployqt.exe" @('--release','--compiler-runtime','--no-translations','--no-opengl-sw','--dir',$stage,(Join-Path $stage 'muzakere.exe'),(Join-Path $stage 'muz_pdf_worker.exe'))
    Copy-Item -LiteralPath "$qtRoot/plugins/platforms/qoffscreen.dll" -Destination (Join-Path $stage 'platforms')
    [IO.File]::WriteAllText((Join-Path $stage 'qt.conf'),"[Paths]`nPlugins=.`n",[Text.UTF8Encoding]::new($false))
    if ($ProfileFile) {
        $profile=Get-Content -Raw -Encoding UTF8 -LiteralPath $ProfileFile | ConvertFrom-Json
        if (!$profile.lawyer -or !$profile.address) { throw 'Profile must contain lawyer and address.' }
        Copy-Item -LiteralPath $ProfileFile -Destination (Join-Path $stage 'response-profile.json')
    }
    Copy-Item -LiteralPath 'docs/testing-installer.md' -Destination (Join-Path $stage 'TEST-README.md')
    Invoke-Checked "$projectRoot/.tools/python/Scripts/python.exe" @('scripts/package-licenses.py',$stage)
    $output=Join-Path $projectRoot 'out/installer'
    New-Item -ItemType Directory -Path $output -Force | Out-Null
    Invoke-Checked $compiler @("/DSourceDir=$stage","/DOutputDir=$output",'packaging/muzakere.iss')
    [IO.File]::WriteAllText((Join-Path $output 'package-stage.txt'),$stage,[Text.UTF8Encoding]::new($false))
    Get-FileHash -LiteralPath (Join-Path $output 'setup.exe') -Algorithm SHA256 | Format-List
    Write-Host "Installer: $output\setup.exe"
} finally {Pop-Location}
