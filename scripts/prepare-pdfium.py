"""Build-time packaging only. The C++ executable loads the native PDFium library."""
from pathlib import Path
from importlib.metadata import distribution
import shutil
import pypdfium2_raw

root = Path(__file__).resolve().parent.parent
target = root / '.tools' / 'pdfium'
target.mkdir(parents=True, exist_ok=True)
source = Path(pypdfium2_raw.__file__).parent
for name in ('pdfium.dll', 'libpdfium.so', 'libpdfium.dylib', 'version.json'):
    if (source / name).exists():
        shutil.copy2(source / name, target / name)
dist = distribution('pypdfium2')
for entry in dist.files or []:
    if '/licenses/' in str(entry).replace('\\', '/'):
        relative = str(entry).replace('\\', '/').split('/licenses/', 1)[1]
        dest = target / 'licenses' / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(dist.locate_file(entry), dest)
print('Native PDFium and third-party notices prepared in .tools/pdfium')
