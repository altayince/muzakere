# Müzakere test kurulumu

Windows 10 (1809+) veya Windows 11, 64 bit gerekir. `setup.exe` dosyasını açıp
kurulum adımlarını tamamlayın. Python, Qt, Office veya derleyici kurmanız gerekmez.
Program internet bağlantısı olmadan çalışır.

1. **Muhasebeye gönderme kısmı** sekmesinden ZIP alın. Satırları inceleyip kaydedin.
2. **Muhasebesiz hamdata oluştur** yalnızca HAMDATA dosya listesini;
   **Muhasebeli ham data oluştur** ayrıca tekil borçluların MUHASEBE sayfasını üretir.
3. MUHASEBE'de A kimlik, B borçlu, C hesap referansı, D tutardır. C/D boş çıkar.
   Muhasebe tutarı yazar veya boş bırakır. HAMDATA satırlarını koruyun.
4. **Muhasebeden onay aldıktan sonra** sekmesinden dönen Excel'i yükleyin.
5. Aynı T.C./vergi numarasının tutarı bütün dosyalarına uygulanır. Sayı (sıfır ve
   negatif sayılar dahil) VAR, boş tutar YOK olur. Hesap referansı tutar değildir.
   Eksik/çift muhasebe eşleşmesi, hatalı tutar ve birden fazla hesap tutarı inceleme
   gerektirir. Şablonlar 89/1 cevapları içindir.
6. Vekil/adres bilgilerini kontrol edin, önizlemeyi okuyun, **Uygun satırları seç**
   ve **Seçili PDF'leri oluştur** düğmelerini kullanın. Her satıra ayrı PDF üretilir.

Artık rastgele muhasebe dönüşü üretilmez; eldeki gerçek HAMDATA/MUHASEBE Excel'i
ikinci sekmeye doğrudan yüklenir. Aynı borçlunun farklı icra dosyaları ayrı PDF olur.
Kimliği eksik kayıtlar HAMDATA'da kalır; ad benzerliğiyle birleştirilmez.
Son cevap tarihi ve kanal kaynaktan bilinmiyorsa boş bırakılır. İİK 78 sayfasındaki
ek isimler, HAMDATA'da dosya ayrıntısı olmadan 89/1 PDF'ine dönüştürülmez.

PDF'ler sağlanan şablon metinlerini değişken alanlarla yeniden düzenler; imza
görseli veya elektronik imza eklemez. Gönderim manuel yapılır. Şablondaki beyanlar
ve özellikle VAR metnindeki ödeme/haciz ifadeleri gönderimden önce kontrol edilir.

**TEST — Veritabanını sıfırla** her iki sekmenin kayıtlarını siler; kaynak ZIP/PDF,
Excel ve üretilen cevap dosyaları diskte kalır. Programı kaldırmak çalışma
verilerini silmez. Veriler `%LOCALAPPDATA%\Muzakere\Muzakere\workspace` altındadır.

Bu paket imzasız bir test sürümüdür. Kurulum Qt/PDF bileşenlerini ve lisanslarını
birlikte getirir; Qt kaynak arşivi `third-party-sources` klasöründedir.
