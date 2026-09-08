param([string]$Installer='out/installer/setup.exe')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$installerPath=(Resolve-Path -LiteralPath $Installer).Path
$testRoot=Join-Path $projectRoot ('out/install-smoke-'+[guid]::NewGuid().ToString('N'))
$installDirectory=Join-Path $testRoot 'app'
$existing='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{4A40F483-041B-44B8-8BC8-8EC7A2F3D7E2}_is1'
if (Test-Path -LiteralPath $existing) { throw 'An existing Müzakere installation is registered; refusing to replace it for a smoke test.' }
New-Item -ItemType Directory -Path $testRoot | Out-Null
$blankPdf=Join-Path $testRoot 'blank.pdf'
& "$projectRoot/.tools/python/Scripts/python.exe" "$projectRoot/scripts/make-smoke-pdf.py" $blankPdf
if ($LASTEXITCODE -ne 0) { throw 'Synthetic PDF creation failed.' }
$savedPath=$env:PATH;$savedPlugin=$env:QT_PLUGIN_PATH;$savedPlatform=$env:QT_QPA_PLATFORM
$installed=$false
try {
    $arguments=@('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-','/NOICONS','/MERGETASKS=!desktopicon',('/DIR="'+$installDirectory+'"'))
    $setup=Start-Process -FilePath $installerPath -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    if ($setup.ExitCode -ne 0) { throw "Installer failed: $($setup.ExitCode)" }
    $installed=$true
    $env:PATH="$env:WINDIR\System32;$env:WINDIR"
    $env:QT_PLUGIN_PATH=$null;$env:QT_QPA_PLATFORM='offscreen'
    $application=Start-Process -FilePath (Join-Path $installDirectory 'muzakere.exe') -ArgumentList '--smoke-test' -WindowStyle Hidden -Wait -PassThru
    if ($application.ExitCode -ne 0) { throw "Installed application smoke failed: $($application.ExitCode)" }
    $worker=New-Object System.Diagnostics.Process
    $worker.StartInfo.FileName=Join-Path $installDirectory 'muz_pdf_worker.exe'
    $worker.StartInfo.UseShellExecute=$false;$worker.StartInfo.CreateNoWindow=$true
    $worker.StartInfo.RedirectStandardInput=$true;$worker.StartInfo.RedirectStandardOutput=$true
    if (!$worker.Start()) { throw 'Worker failed to start.' }
    $pdfBytes=[IO.File]::ReadAllBytes($blankPdf)
    $worker.StandardInput.BaseStream.Write($pdfBytes,0,$pdfBytes.Length);$worker.StandardInput.Close()
    $response=$worker.StandardOutput.ReadToEnd();$worker.WaitForExit()
    if ($worker.ExitCode -ne 0) { throw 'Installed PDF worker failed.' }
    $parsed=$response | ConvertFrom-Json
    if ($parsed.error -or $parsed.text -notmatch 'MUZ PACKAGE TEST') { throw 'Installed PDFium validation failed.' }
    Write-Host 'PASS: setup.exe installed; desktop, SQLite and PDFium work without developer PATH or Python at runtime.'
} finally {
    $env:PATH=$savedPath;$env:QT_PLUGIN_PATH=$savedPlugin;$env:QT_QPA_PLATFORM=$savedPlatform
    if ($installed) {
        $resolved=(Resolve-Path -LiteralPath $installDirectory).Path
        $expected=[IO.Path]::GetFullPath((Join-Path $projectRoot 'out'))+[IO.Path]::DirectorySeparatorChar
        if (!$resolved.StartsWith($expected,[StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe test uninstall path.' }
        $uninstaller=Join-Path $resolved 'unins000.exe'
        if (Test-Path -LiteralPath $uninstaller) {
            $remove=Start-Process -FilePath $uninstaller -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART' -WindowStyle Hidden -Wait -PassThru
            if ($remove.ExitCode -ne 0) { throw 'Test uninstall failed.' }
        }
    }
}
