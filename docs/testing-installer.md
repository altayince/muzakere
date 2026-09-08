# Müzakere test kurulumu

Windows 10 (1809+) veya Windows 11, 64 bit gerekir. `setup.exe` dosyasını açıp
kurulum adımlarını tamamlayın. Python, Qt, Office veya derleyici kurmanız gerekmez.
Program internet bağlantısı olmadan çalışır.

1. **Muhasebeye gönderme kısmı** sekmesinden ZIP alın. Satırları inceleyip onaylayın.
2. **Onaylı Excel** ile dosya oluşturun. En sağdaki **Muhasebe** sütunu boştur.
3. Muhasebe bu sütuna tutarı yazar veya hücreyi boş bırakır. Diğer sütunları ve
   **Kaynaklar** sayfasını değiştirmeyin; bütün satırlarla sıralama yapılabilir.
4. **Muhasebeden onay aldıktan sonra** sekmesinden dönen Excel'i yükleyin.
5. Sayı (sıfır dahil) VAR, boş hücre YOK olur. Metin, formül, tarih ve geçersiz
   dosya/borçlu eşleşmeleri inceleme gerektirir. Şablonlar 89/1 cevapları içindir.
6. Vekil/adres bilgilerini kontrol edin, önizlemeyi okuyun, **Uygun satırları seç**
   ve **Seçili PDF'leri oluştur** düğmelerini kullanın. Her satıra ayrı PDF üretilir.

Muhasebeyi beklemeden denemek için ilk sekmedeki **Test: muhasebe dönüşü oluştur**
düğmesi bazı hücreleri rastgele sayılarla dolduran ayrı bir dosya üretir. Bu
dosyadan oluşturulan PDF'lerde **TEST VERİSİ — TASLAK** ibaresi bulunur.

PDF'ler sağlanan şablon metinlerini değişken alanlarla yeniden düzenler; imza
görseli veya elektronik imza eklemez. Gönderim manuel yapılır. Şablondaki beyanlar
ve özellikle VAR metnindeki ödeme/haciz ifadeleri gönderimden önce kontrol edilir.

**TEST — Veritabanını sıfırla** her iki sekmenin kayıtlarını siler; kaynak ZIP/PDF,
Excel ve üretilen cevap dosyaları diskte kalır. Programı kaldırmak çalışma
verilerini silmez. Veriler `%LOCALAPPDATA%\Muzakere\Muzakere\workspace` altındadır.

Bu paket imzasız bir test sürümüdür. Kurulum Qt/PDF bileşenlerini ve lisanslarını
birlikte getirir; Qt kaynak arşivi `third-party-sources` klasöründedir.
