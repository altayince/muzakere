$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$compilerSetup=Join-Path $projectRoot '.tools/innosetup-6.4.3.exe'
$compilerDirectory=Join-Path $projectRoot '.tools/InnoSetup'
$qtSource=Join-Path $projectRoot '.tools/qtbase-everywhere-src-6.8.3.tar.xz'
function Download-Verified([string]$Url,[string]$Destination,[string]$Sha256) {
    if (!(Test-Path -LiteralPath $Destination)) { Invoke-WebRequest -Uri $Url -OutFile $Destination }
    if ((Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash -ne $Sha256) { throw "Unexpected SHA-256: $Destination" }
}
New-Item -ItemType Directory -Path (Join-Path $projectRoot '.tools') -Force | Out-Null
Download-Verified 'https://github.com/jrsoftware/issrc/releases/download/is-6_4_3/innosetup-6.4.3.exe' $compilerSetup 'F3C42116542C4CC57263C5BA6C4FEABFC49FE771F2F98A79D2F7628B8762723B'
if (!(Test-Path -LiteralPath (Join-Path $compilerDirectory 'ISCC.exe'))) {
    if ((Get-AuthenticodeSignature -LiteralPath $compilerSetup).Status -ne 'Valid') { throw 'Inno Setup signature verification failed.' }
    $compilerArguments=@('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-','/CURRENTUSER','/NOICONS',('/DIR="'+$compilerDirectory+'"'))
    $process=Start-Process -FilePath $compilerSetup -ArgumentList $compilerArguments -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw 'Inno Setup installation failed.' }
}
Download-Verified 'https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz' $qtSource '56001B905601BB9023D399F3BA780D7FA940F3E4861E496A7C490331F49E0B80'
Write-Host 'Installer compiler and Qt source archive are ready.'
