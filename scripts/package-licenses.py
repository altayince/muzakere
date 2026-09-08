"""Collect redistribution notices and matching Qt source; build-time only."""
import hashlib
from pathlib import Path
import shutil
import sys
import tarfile

root=Path(__file__).resolve().parent.parent
stage=Path(sys.argv[1]).resolve()
if not stage.is_relative_to(root/'out'):
    raise SystemExit('The package stage must be inside the project out directory.')
licenses=stage/'licenses'
licenses.mkdir(exist_ok=True)
source=root/'.tools/qtbase-everywhere-src-6.8.3.tar.xz'
if hashlib.file_digest(source.open('rb'),'sha256').hexdigest()!='56001b905601bb9023d399f3ba780d7fa940f3e4861e496a7c490331f49e0b80':
    raise SystemExit('Unexpected Qt source hash')
qt=licenses/'Qt';qt.mkdir(exist_ok=True)
with tarfile.open(source) as archive:
    for member in archive.getmembers():
        if '/LICENSES/' in member.name and member.isfile() and member.size<1024*1024:
            (qt/Path(member.name).name).write_bytes(archive.extractfile(member).read())
for dependency,local in [('qxlsx','QXlsx'),('miniz','miniz')]:
    license_file=root/f'.tools/{local}/LICENSE'
    if not license_file.is_file():
        license_file=root/f'build/release/_deps/{dependency}-src/LICENSE'
    shutil.copy2(license_file,licenses/f'{local}.txt')
shutil.copytree(root/'.tools/pdfium/licenses',licenses/'PDFium',dirs_exist_ok=True)
shutil.copytree(root/'.tools/Qt/Tools/mingw1310_64/licenses',licenses/'MinGW',dirs_exist_ok=True)
shutil.copy2(root/'.tools/InnoSetup/license.txt',licenses/'InnoSetup.txt')
sources=stage/'third-party-sources';sources.mkdir(exist_ok=True)
shutil.copy2(source,sources/source.name)
(licenses/'README.txt').write_text(
    'Muzakere uses shared Qt 6.8.3 libraries (LGPLv3), PDFium, QXlsx, miniz and the MinGW runtimes.\n'
    'Their license notices are in this directory. Matching Qt Base source is included in ../third-party-sources.\n'
    'You may replace compatible shared Qt libraries and reverse engineer this application for debugging modifications to those libraries, as permitted by the LGPL.\n'
    'Qt source/build information: https://doc.qt.io/qt-6.8/build-sources.html\n'
    'Installer created with Inno Setup 6.4.3: https://jrsoftware.org/\n',encoding='utf-8')
