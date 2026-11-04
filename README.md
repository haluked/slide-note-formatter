# 🎓 Slide Note Formatter (Slayt Not Formatlayıcı) v2.0

<p align="center">
  <img src="assets/icon.png" width="112" height="112" alt="Slide Note Formatter Icon" />
</p>

<p align="center">
  <strong>Ders slayt sunumlarını sağ tarafı geniş not alma alanlı, kurum armalı A4 fotokopi dokümanlarına dönüştürün.</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Platform-Windows-0078D6?logo=windows&logoColor=white" alt="Platform Windows" />
  <img src="https://img.shields.io/badge/Stack-Tauri%20v2%20(Rust)%20%2B%20Tailwind%20UI-000000?logo=tauri&logoColor=white" alt="Tauri + Tailwind" />
  <img src="https://img.shields.io/badge/Frontend-React%20%2F%20Vite%20%2F%20TypeScript-61DAFB?logo=react&logoColor=black" alt="React Vite" />
  <img src="https://img.shields.io/badge/Styling-Tailwind%20CSS-38B2AC?logo=tailwind-css&logoColor=white" alt="Tailwind CSS" />
  <img src="https://img.shields.io/badge/License-MIT-green.svg" alt="License MIT" />
</p>

> ⚡ **v2.0 Modern Rewrite**: Uygulama, eski Tkinter masaüstü arayüzünden tamamen arındırılarak **Tauri v2 (Rust)** çekirdeği ve **Tailwind UI (React + TypeScript)** ile modern, akıcı ve yüksek performanslı bir masaüstü uygulaması olarak yeniden inşa edilmiştir.

---

## 💡 Neden Slide Note Formatter?

Üniversite ve okul derslerinde slaytları fotokopi veya tablet çıktısı alırken standart PDF okuyucular (PDF24 vb.) sayfayı ortalar veya kenarlardan eşit daraltarak sağ tarafta not alma alanı bırakmaz. 

**Slide Note Formatter**, ders sunumlarını doğrudan çalışma fasikülüne dönüştürür:
* **Sola Hizalı Slaytlar:** Standart A4 dikey kağıtta slaytları sol tarafa yerleştirir; zımba payı bırakır.
* **Geniş Not Alma Alanı:** Sayfanın sağ tarafında boydan boya **çizgili not tutma alanı** sağlar.
* **Resmi Ön Kapak & Bitiş:** Üniversite/kurum armasıyla, ders adı, konu ve öğrenci bilgisi içeren zarif bir ön kapak ve modül kapanış sayfası ekler.
* **Canlı A4 Sayfa & Kapak Önizlemesi:** İşlemi başlatmadan önce hem not sayfasının hem de ön kapağın baskıda nasıl çıkacağını birebir canlı simüle eder.
* **100% Çevrimdışı ve Güvenli:** Belgeleriniz hiçbir harici sunucuya yüklenmez; her şey cihazınızda yerel olarak işlenir.

---

## ✨ Özellikler

| Özellik | Açıklama |
|---|---|
| **Tauri v2 + Rust Çekirdeği** | Düşük bellek tüketimi, yerel Windows dosya diyalogları ve sistemle tam entegrasyon. |
| **Tailwind UI & Apple HIG** | Apple Human Interface Guidelines ve Emil Kowalski tasarım prensiplerine uygun modern karanlık/aydınlık arayüz. |
| **İkili Canlı Önizleme** | `Not Sayfası` ve `Ön Kapak` arasında anında geçiş yaparak nihai tasarımı inceleyin. |
| **Sürükle & Bırak Dosya Kuyruğu** | PDF dosyalarını doğrudan arayüze sürükleyin; tabular monospace dosya boyutlarını (`2.4 MB`) görün. |
| **Klavye Kısayolları** | `Ctrl+O` (Dosya Ekle), `Ctrl+Shift+O` (Klasör Tara), `Ctrl+↵` (Başlat), `Esc` (Temizle/Kapat). |
| **Akıllı Ders Adı Algılama** | Dosya adından (örn. `ELEC201`) veya üst klasör adından ders kodunu otomatik çıkarır. |
| **Kalıcı Profil Hatırlama** | Adınızı, üniversite adınızı ve alt başlığınızı kaydeder; her açılışta hatırlar. |
| **Baskı & Toner Optimizasyonu** | 100/144/200 DPI baskı netliği ve siyah-beyaz fotokopi için gri tonlama seçeneği. |

---

## 🚀 Başlangıç

### Seçenek 1: Bağımsız `.exe` İle Çalıştırma (Kurulumsuz)
`dist/SlideNoteFormatter.exe` dosyasını doğrudan çalıştırabilirsiniz.

### Seçenek 2: Geliştirici Modunda Çalıştırma

1. **Gereksinimler:** Node.js (v18+) ve Rust (v1.75+)
2. **Bağımlılıkları yükleyin:**
   ```bash
   npm install
   ```
3. **Geliştirici modunda başlatın (Hot-reload):**
   ```bash
   npm run tauri dev
   ```

---

## 🛠️ Kendi `.exe` Dosyanızı Derleme

Proje kök dizinindeki `build.bat` dosyasını çift tıklayarak çalıştırabilirsiniz:

```bat
build.bat
```

Derleme tamamlandığında bağımsız Windows uygulaması `dist\SlideNoteFormatter.exe` konumunda hazır olur.

---

## 📂 Proje Yapısı

```
slide-note-formatter/
├── src/                  # React + TypeScript + Tailwind UI
│   ├── App.tsx           # Ana masaüstü arayüzü ve canlı önizleme
│   ├── index.css         # Tailwind CSS stilleri
│   └── main.tsx          # React giriş noktası
├── src-tauri/            # Tauri v2 (Rust) Masaüstü Çekirdeği
│   ├── Cargo.toml        # Rust bağımlılıkları (tauri, rfd, walkdir, serde)
│   ├── tauri.conf.json   # Pencere, güvenlik ve uygulama yapılandırması
│   └── src/lib.rs        # IPC komutları ve worker yürütücüsü
├── engine/               # Headless Python PDF Dönüştürme Motoru
│   ├── worker.py         # Tauri için streaming JSON worker CLI
│   ├── pdf_engine.py     # Yüksek kaliteli A4 not sayfası birleştirici
│   └── assets.py         # Vektörel logo & arma varlıkları
├── assets/               # İkonlar ve görseller
├── build.bat             # Tek tıkla otomatik tam derleyici
└── package.json          # Node.js bağımlılıkları ve scriptler
```

---

## 📄 Lisans

Bu proje [MIT Lisansı](LICENSE) kapsamında açık kaynaklıdır. Geliştirici: **Haluk Tanrıseven** ([@haluked](https://github.com/haluked))
