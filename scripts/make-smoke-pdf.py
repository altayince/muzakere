"""Write a tiny synthetic PDF for testing the deployed native worker."""
from pathlib import Path
import sys
stream=b'BT /F1 12 Tf 10 50 Td (MUZ PACKAGE TEST) Tj ET'
objects=[b'<< /Type /Catalog /Pages 2 0 R >>',b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
    b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 100] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>',
    f'<< /Length {len(stream)} >>\nstream\n'.encode()+stream+b'\nendstream',
    b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>']
data=bytearray(b'%PDF-1.4\n');offsets=[]
for index,content in enumerate(objects,1):
    offsets.append(len(data));data+=f'{index} 0 obj\n'.encode()+content+b'\nendobj\n'
xref=len(data);data+=b'xref\n0 6\n0000000000 65535 f \n'
for offset in offsets:data+=f'{offset:010d} 00000 n \n'.encode()
data+=f'trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n'.encode()
Path(sys.argv[1]).write_bytes(data)
