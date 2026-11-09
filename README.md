# Slide Note Formatter

**v2.1 · Native C11 / Win32 · Windows 10/11 · MIT**

> Ders slaytlarını sağ tarafta geniş not alma alanı bırakan, kurum armalı A4 çalışma dokümanlarına dönüştürür.

Bu depo, uygulamanın **tek aktif sürümünü** içerir: saf C11 / Win32 ile yazılmış, Python, Electron veya Tauri çalışma zamanı gerektirmeyen yerel derleme.

![Slide Note Formatter](assets/screenshot.png)

## Özellikler

- **Retro GB arayüz:** dört tonlu DMG paleti, piksel köşeler, özel çizilmiş butonlar, checkbox'lar, dropdown, dosya listesi ve mesaj kutuları.
- **Toplu dönüştürme:** Tek PDF veya birden fazla PDF'yi aynı anda işler.
- **Sayfa düzeni:** Sayfa başına 2, 3 veya 4 slayt.
- **Slayt kenarlığı:** İsteğe bağlı ince çerçeve.
- **Ön kapak ve bitiş sayfası:** Ders adı, alt başlık, öğrenci adı ve kurum bilgisiyle.
- **Esnek çıktı:** Kaynak dosyanın yanında `_formatted` alt klasörü veya seçilen özel klasör.
- **Sürükle-bırak:** PDF dosyalarını doğrudan pencereye bırakabilirsiniz.
- **İlerleme çubuğu:** Dönüştürme sırasında GB tarzı ilerleme göstergesi.
- **Tamamen çevrimdışı:** Dosyalar hiçbir sunucuya yüklenmez.
- **Hafif:** Yaklaşık 250 KB tek dosya; harici çalışma zamanı yok.

## İndirme

En son sürüm: [GitHub Releases](https://github.com/haluked/slide-note-formatter/releases/latest)

`SlideNoteFormatter.exe` dosyasını çalıştırmanız yeterlidir; kurulum gerekmez.

## Derleme

Gereksinimler:

- MinGW-w64 GCC (C11)
- `windres`
- zlib (`-lz`)

Tek komut:

```cmd
build.bat
```

Çıktı: `dist\SlideNoteFormatter.exe`

## Kullanım

### GUI

1. **BROWSE...** ile PDF seçin veya dosyaları pencereye sürükleyin.
2. Sayfa düzenini ve isteğe bağlı kapak bilgilerini ayarlayın.
3. **CONVERT PDFS** düğmesine basın.

### CLI

```cmd
SlideNoteFormatter.exe <input.pdf> [output.pdf] [slides_per_page] [-author "Name"] [-course "Course"] [-module "Subtitle"] [-uni "Institution"]
```

Örnek:

```cmd
SlideNoteFormatter.exe lecture.pdf output.pdf 3 -author "Haluk Tanriseven"
```

## Tasarım

Retro GB tasarım sistemi, palet, tipografi, ölçüler ve bileşen kuralları için [DESIGN.md](DESIGN.md) dosyasına bakın.

## Proje yapısı

```
slide-note-formatter/
├── src_c/
│   ├── main.c          # Win32 arayüzü ve CLI
│   ├── gb_ui.c         # GB paleti, çizim ve özel kontroller
│   ├── gb_ui.h
│   ├── pdf_engine.c    # PDF dönüştürme motoru
│   ├── pdf_engine.h
│   ├── res.rc          # ikon ve sürüm bilgisi
│   └── res.manifest    # Per-Monitor V2 DPI
├── assets/             # ikon, kurum logosu ve ekran görüntüsü
├── build.bat
├── DESIGN.md
├── LICENSE
└── README.md
```

## Lisans

MIT. Ayrıntılar için [LICENSE](LICENSE).
