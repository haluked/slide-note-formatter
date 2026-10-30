import os
import sys
import threading
import traceback
import webbrowser
import fitz
from PIL import Image
import customtkinter as ctk
from tkinter import filedialog, messagebox

# UTF-8 encoding support
if sys.stdout and hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

from config import ConfigManager
from pdf_engine import process_pdf
from assets import get_logo_bytes, get_app_icon_path


def detect_course_name(paths):
    for p in paths:
        low = p.lower()
        if "electrical" in low or "elec" in low:
            return "ELECTRICAL MACHINES"
        elif "machine design" in low or "fmd" in low:
            return "FUNDAMENTALS OF MACHINE DESIGN"
        elif "automatic control" in low:
            return "AUTOMATIC CONTROL"
        elif "motor vehicle" in low or "mvd" in low:
            return "MOTOR VEHICLE DESIGN"
        elif "circuit" in low:
            return "ELECTRIC CIRCUITS"
    return "COURSE"


class SlideNoteApp(ctk.CTk):
    def __init__(self):
        super().__init__()

        self.cfg = ConfigManager()

        self.title("PoliTo Slayt Not Formatlayıcı")
        self.geometry("920, 760")
        self.minsize(840, 660)

        # Apply theme
        ctk.set_appearance_mode(self.cfg.get("appearance_mode", "dark"))
        ctk.set_default_color_theme("blue")

        # Set Window Icon
        icon_path = get_app_icon_path()
        if icon_path and os.path.exists(icon_path):
            try:
                self.iconbitmap(icon_path)
            except Exception:
                pass

        self.selected_files = []
        self.is_processing = False
        self.last_output_dir = None

        self.setup_ui()

    def setup_ui(self):
        # Top Header Bar
        header = ctk.CTkFrame(self, fg_color="transparent")
        header.pack(fill="x", padx=24, pady=(16, 8))

        title_lbl = ctk.CTkLabel(
            header,
            text="🎓 PoliTo Slayt Not Formatlayıcı",
            font=ctk.CTkFont(size=22, weight="bold")
        )
        title_lbl.pack(side="left")

        self.lbl_profile_tag = ctk.CTkLabel(
            header,
            text=f"👤 {self.cfg.get('user_name')} | 🏛️ {self.cfg.get('university_name')}",
            font=ctk.CTkFont(size=12),
            text_color="#94A3B8"
        )
        self.lbl_profile_tag.pack(side="right", padx=4)

        # Tab View
        self.tabview = ctk.CTkTabview(self, corner_radius=12)
        self.tabview.pack(fill="both", expand=True, padx=24, pady=(0, 16))

        self.tab_convert = self.tabview.add("📑 Dönüştürücü")
        self.tab_profile = self.tabview.add("👤 Profil & Logo")
        self.tab_settings = self.tabview.add("⚙️ Detaylı Ayarlar")
        self.tab_about = self.tabview.add("ℹ️ Hakkında")

        self.setup_converter_tab()
        self.setup_profile_tab()
        self.setup_settings_tab()
        self.setup_about_tab()

    # -------------------------------------------------------------
    # TAB 1: CONVERTER
    # -------------------------------------------------------------
    def setup_converter_tab(self):
        parent = self.tab_convert

        main_split = ctk.CTkFrame(parent, fg_color="transparent")
        main_split.pack(fill="both", expand=True, padx=8, pady=8)

        # LEFT COLUMN: File list & controls
        left_col = ctk.CTkFrame(main_split)
        left_col.pack(side="left", fill="both", expand=True, padx=(0, 10))

        left_lbl = ctk.CTkLabel(left_col, text="📂 Slayt PDF Dosyaları", font=ctk.CTkFont(size=14, weight="bold"))
        left_lbl.pack(anchor="w", padx=14, pady=(12, 6))

        btn_row = ctk.CTkFrame(left_col, fg_color="transparent")
        btn_row.pack(fill="x", padx=14, pady=4)

        self.btn_add_files = ctk.CTkButton(btn_row, text="+ Dosya(lar) Seç", command=self.add_files, width=120)
        self.btn_add_files.pack(side="left", padx=(0, 6))

        self.btn_add_folder = ctk.CTkButton(btn_row, text="📁 Klasör Tara", command=self.add_folder, width=110, fg_color="#3B82F6")
        self.btn_add_folder.pack(side="left", padx=6)

        self.btn_clear = ctk.CTkButton(btn_row, text="Temizle", command=self.clear_files, width=70, fg_color="#EF4444", hover_color="#DC2626")
        self.btn_clear.pack(side="right")

        self.file_textbox = ctk.CTkTextbox(left_col, font=ctk.CTkFont(family="Consolas", size=12), wrap="none")
        self.file_textbox.pack(fill="both", expand=True, padx=14, pady=10)
        self.file_textbox.insert("1.0", "Henüz dosya seçilmedi.\nYukarıdaki butonlarla bir veya birden fazla PDF ekleyin...")
        self.file_textbox.configure(state="disabled")

        self.lbl_file_count = ctk.CTkLabel(left_col, text="Toplam: 0 dosya seçildi", text_color="gray", font=ctk.CTkFont(size=12))
        self.lbl_file_count.pack(anchor="w", padx=14, pady=(0, 10))

        # RIGHT COLUMN: Model options & Output
        right_col = ctk.CTkFrame(main_split)
        right_col.pack(side="right", fill="both", expand=True, padx=(10, 0))

        right_lbl = ctk.CTkLabel(right_col, text="⚙️ Model & Hedef", font=ctk.CTkFont(size=14, weight="bold"))
        right_lbl.pack(anchor="w", padx=14, pady=(12, 6))

        # Preset Selector
        preset_frame = ctk.CTkFrame(right_col, fg_color="#1E293B", corner_radius=10)
        preset_frame.pack(fill="x", padx=14, pady=6)

        ctk.CTkLabel(preset_frame, text="Preset (Şablon):", font=ctk.CTkFont(weight="bold")).pack(anchor="w", padx=12, pady=(10, 4))

        self.preset_var = ctk.StringVar(value=self.cfg.get("default_preset"))
        self.preset_menu = ctk.CTkOptionMenu(
            preset_frame,
            values=[
                "Bizim Model (3 Slayt + PoliTo Kapak)",
                "Bizim Model (2 Slayt + PoliTo Kapak)",
                "3 Slayt (Sadece Not Alanı - Kapaksız)",
                "2 Slayt (Sadece Not Alanı - Kapaksız)",
                "4 Slayt (Kompakt Not Alanı)"
            ],
            variable=self.preset_var,
            command=self.on_preset_selected
        )
        self.preset_menu.pack(fill="x", padx=12, pady=(0, 10))

        # Course Name Entry
        ctk.CTkLabel(right_col, text="Ders Adı (Kapak Sayfası):", font=ctk.CTkFont(size=12, weight="bold")).pack(anchor="w", padx=14, pady=(8, 2))
        self.ent_course = ctk.CTkEntry(right_col, placeholder_text="Otomatik algılanır veya yazın")
        self.ent_course.insert(0, "FUNDAMENTALS OF MACHINE DESIGN")
        self.ent_course.pack(fill="x", padx=14, pady=(0, 8))

        # Output Destination Box
        dest_box = ctk.CTkFrame(right_col, fg_color="#1E293B", corner_radius=10)
        dest_box.pack(fill="x", padx=14, pady=6)

        ctk.CTkLabel(dest_box, text="📁 Kayıt Konumu:", font=ctk.CTkFont(weight="bold")).pack(anchor="w", padx=12, pady=(10, 4))

        self.dest_mode_var = ctk.StringVar(value=self.cfg.get("dest_mode", "subfolder"))
        self.rb_sub = ctk.CTkRadioButton(
            dest_box,
            text="Aynı klasörde alt klasöre kaydet (_Not_Formatli/)",
            variable=self.dest_mode_var,
            value="subfolder",
            command=self.on_dest_mode_changed
        )
        self.rb_sub.pack(anchor="w", padx=12, pady=4)

        self.rb_cust = ctk.CTkRadioButton(
            dest_box,
            text="Özel bir klasöre kaydet:",
            variable=self.dest_mode_var,
            value="custom",
            command=self.on_dest_mode_changed
        )
        self.rb_cust.pack(anchor="w", padx=12, pady=4)

        cust_row = ctk.CTkFrame(dest_box, fg_color="transparent")
        cust_row.pack(fill="x", padx=12, pady=(0, 10))

        self.ent_dest_path = ctk.CTkEntry(cust_row, placeholder_text="Klasör seçilmedi", state="disabled")
        self.ent_dest_path.pack(side="left", fill="x", expand=True, padx=(0, 6))

        self.btn_browse_dest = ctk.CTkButton(cust_row, text="Gözat...", width=70, command=self.browse_custom_dest, state="disabled")
        self.btn_browse_dest.pack(side="right")

        # Bottom Action Bar
        bottom_box = ctk.CTkFrame(parent, fg_color="transparent")
        bottom_box.pack(fill="x", padx=8, pady=(10, 4))

        self.progress_bar = ctk.CTkProgressBar(bottom_box)
        self.progress_bar.pack(fill="x", pady=(0, 8))
        self.progress_bar.set(0)

        action_row = ctk.CTkFrame(bottom_box, fg_color="transparent")
        action_row.pack(fill="x")

        self.lbl_status = ctk.CTkLabel(action_row, text="Hazır", font=ctk.CTkFont(size=13), text_color="#94A3B8")
        self.lbl_status.pack(side="left")

        self.btn_open_folder = ctk.CTkButton(
            action_row,
            text="📂 Çıktı Klasörünü Aç",
            command=self.open_output_folder,
            width=150,
            fg_color="#475569",
            hover_color="#334155",
            state="disabled"
        )
        self.btn_open_folder.pack(side="right", padx=(10, 0))

        self.btn_start = ctk.CTkButton(
            action_row,
            text="🚀 Dönüştürmeyi Başlat",
            font=ctk.CTkFont(size=14, weight="bold"),
            command=self.start_conversion,
            width=180,
            height=38,
            fg_color="#10B981",
            hover_color="#059669"
        )
        self.btn_start.pack(side="right")

    # -------------------------------------------------------------
    # TAB 2: PROFILE & UNIVERSITY
    # -------------------------------------------------------------
    def setup_profile_tab(self):
        parent = self.tab_profile

        scroll = ctk.CTkScrollableFrame(parent)
        scroll.pack(fill="both", expand=True, padx=14, pady=14)

        ctk.CTkLabel(scroll, text="👤 Kişisel Profil & Üniversite Bilgileri", font=ctk.CTkFont(size=16, weight="bold")).pack(anchor="w", pady=(0, 4))
        ctk.CTkLabel(scroll, text="Bu bilgiler otomatik hatırlanır ve tüm kapak/filigran işlemlerine uygulanır.", font=ctk.CTkFont(size=12), text_color="gray").pack(anchor="w", pady=(0, 16))

        # User Name
        ctk.CTkLabel(scroll, text="Öğrenci / Kullanıcı Adı (Filigran & Kapak):", font=ctk.CTkFont(weight="bold")).pack(anchor="w", pady=(4, 2))
        self.ent_prof_name = ctk.CTkEntry(scroll, width=400)
        self.ent_prof_name.insert(0, self.cfg.get("user_name"))
        self.ent_prof_name.pack(anchor="w", pady=(0, 12))

        # University Name
        ctk.CTkLabel(scroll, text="Üniversite / Kurum Adı (Kapak):", font=ctk.CTkFont(weight="bold")).pack(anchor="w", pady=(4, 2))
        self.ent_prof_uni = ctk.CTkEntry(scroll, width=400)
        self.ent_prof_uni.insert(0, self.cfg.get("university_name"))
        self.ent_prof_uni.pack(anchor="w", pady=(0, 12))

        # Subtitle
        ctk.CTkLabel(scroll, text="Kapak Alt Başlığı:", font=ctk.CTkFont(weight="bold")).pack(anchor="w", pady=(4, 2))
        self.ent_prof_sub = ctk.CTkEntry(scroll, width=400)
        self.ent_prof_sub.insert(0, self.cfg.get("subtitle"))
        self.ent_prof_sub.pack(anchor="w", pady=(0, 16))

        # Logo Configuration Frame
        logo_frame = ctk.CTkFrame(scroll, fg_color="#1E293B", corner_radius=10)
        logo_frame.pack(fill="x", pady=8)

        ctk.CTkLabel(logo_frame, text="🏛️ Kapak Logo Yönetimi", font=ctk.CTkFont(size=14, weight="bold")).pack(anchor="w", padx=16, pady=(12, 4))

        self.lbl_active_logo = ctk.CTkLabel(
            logo_frame,
            text=self.get_active_logo_label(),
            font=ctk.CTkFont(size=12),
            text_color="#38BDF8"
        )
        self.lbl_active_logo.pack(anchor="w", padx=16, pady=(0, 8))

        logo_btn_row = ctk.CTkFrame(logo_frame, fg_color="transparent")
        logo_btn_row.pack(fill="x", padx=16, pady=(0, 12))

        ctk.CTkButton(logo_btn_row, text="📁 Özel Logo Seç (PNG/JPG)...", command=self.select_custom_logo).pack(side="left", padx=(0, 8))
        ctk.CTkButton(logo_btn_row, text="🔄 Politecnico Logosuna Sıfırla", fg_color="#475569", command=self.reset_to_polito_logo).pack(side="left")

        # Save Button
        ctk.CTkButton(
            scroll,
            text="💾 Profil Ayarlarını Kaydet",
            font=ctk.CTkFont(weight="bold"),
            fg_color="#10B981",
            hover_color="#059669",
            height=36,
            command=self.save_profile_settings
        ).pack(anchor="w", pady=16)

    def get_active_logo_label(self):
        cpath = self.cfg.get("custom_logo_path")
        if cpath and os.path.isfile(cpath):
            return f"Aktif Logo: Özel Dosya ({os.path.basename(cpath)})"
        return "Aktif Logo: Politecnico di Torino (Dahili Siyah-Beyaz)"

    def select_custom_logo(self):
        file = filedialog.askopenfilename(
            title="Üniversite/Kurum Logosu Seçin",
            filetypes=[("Görsel Dosyaları", "*.png *.jpg *.jpeg")]
        )
        if file:
            self.cfg.set("custom_logo_path", file)
            self.cfg.save()
            self.lbl_active_logo.configure(text=self.get_active_logo_label())
            messagebox.showinfo("Logo Güncellendi", f"Özel logo seçildi:\n{os.path.basename(file)}")

    def reset_to_polito_logo(self):
        self.cfg.set("custom_logo_path", "")
        self.cfg.save()
        self.lbl_active_logo.configure(text=self.get_active_logo_label())
        messagebox.showinfo("Logo Sıfırlandı", "Varsayılan Politecnico di Torino logosuna dönüldü.")

    def save_profile_settings(self):
        self.cfg.set("user_name", self.ent_prof_name.get().strip())
        self.cfg.set("university_name", self.ent_prof_uni.get().strip())
        self.cfg.set("subtitle", self.ent_prof_sub.get().strip())
        self.cfg.save()

        self.lbl_profile_tag.configure(text=f"👤 {self.cfg.get('user_name')} | 🏛️ {self.cfg.get('university_name')}")
        messagebox.showinfo("Başarılı", "Profil ve üniversite ayarlarınız kalıcı olarak kaydedildi!")

    # -------------------------------------------------------------
    # TAB 3: ADVANCED SETTINGS
    # -------------------------------------------------------------
    def setup_settings_tab(self):
        parent = self.tab_settings

        scroll = ctk.CTkScrollableFrame(parent)
        scroll.pack(fill="both", expand=True, padx=14, pady=14)

        ctk.CTkLabel(scroll, text="⚙️ Görsel Kalite ve Fotokopi Tercihleri", font=ctk.CTkFont(size=16, weight="bold")).pack(anchor="w", pady=(0, 16))

        # DPI
        ctk.CTkLabel(scroll, text="Çözünürlük (DPI):", font=ctk.CTkFont(weight="bold")).pack(anchor="w", pady=(4, 2))
        self.seg_dpi = ctk.CTkSegmentedButton(scroll, values=["100 DPI (Hafif)", "144 DPI (Önerilen)", "200 DPI (Net)"])
        current_dpi = self.cfg.get("dpi", 144)
        if current_dpi == 100:
            self.seg_dpi.set("100 DPI (Hafif)")
        elif current_dpi == 200:
            self.seg_dpi.set("200 DPI (Net)")
        else:
            self.seg_dpi.set("144 DPI (Önerilen)")
        self.seg_dpi.pack(anchor="w", pady=(0, 12))

        # Switched row
        self.switch_gray = ctk.CTkSwitch(scroll, text="Gri Tonlama (Grayscale / Siyah-Beyaz Baskı)", font=ctk.CTkFont(weight="bold"))
        if self.cfg.get("grayscale", True):
            self.switch_gray.select()
        else:
            self.switch_gray.deselect()
        self.switch_gray.pack(anchor="w", pady=8)

        self.switch_border = ctk.CTkSwitch(scroll, text="Slayt Etrafına İnce Gri Çerçeve Ekle", font=ctk.CTkFont(weight="bold"))
        if self.cfg.get("draw_border", False):
            self.switch_border.select()
        else:
            self.switch_border.deselect()
        self.switch_border.pack(anchor="w", pady=8)

        self.switch_cover = ctk.CTkSwitch(scroll, text="Ön Kapak Sayfası Oluştur", font=ctk.CTkFont(weight="bold"))
        if self.cfg.get("add_cover", True):
            self.switch_cover.select()
        else:
            self.switch_cover.deselect()
        self.switch_cover.pack(anchor="w", pady=8)

        self.switch_end = ctk.CTkSwitch(scroll, text="Bitiş Sayfası Oluştur (- END OF MODULE -)", font=ctk.CTkFont(weight="bold"))
        if self.cfg.get("add_end", True):
            self.switch_end.select()
        else:
            self.switch_end.deselect()
        self.switch_end.pack(anchor="w", pady=8)

        # Appearance Theme
        ctk.CTkLabel(scroll, text="Arayüz Teması:", font=ctk.CTkFont(weight="bold")).pack(anchor="w", pady=(12, 2))
        self.seg_theme = ctk.CTkSegmentedButton(scroll, values=["Karanlık", "Aydınlık", "Sistem"], command=self.on_theme_changed)
        current_theme = self.cfg.get("appearance_mode", "dark")
        if current_theme == "light":
            self.seg_theme.set("Aydınlık")
        elif current_theme == "system":
            self.seg_theme.set("Sistem")
        else:
            self.seg_theme.set("Karanlık")
        self.seg_theme.pack(anchor="w", pady=(0, 16))

        # Save Preferences Button
        ctk.CTkButton(
            scroll,
            text="💾 Tercihleri Kaydet",
            font=ctk.CTkFont(weight="bold"),
            fg_color="#10B981",
            hover_color="#059669",
            command=self.save_advanced_settings
        ).pack(anchor="w", pady=12)

    def on_theme_changed(self, value):
        mode_map = {"Karanlık": "dark", "Aydınlık": "light", "Sistem": "system"}
        mode = mode_map.get(value, "dark")
        ctk.set_appearance_mode(mode)
        self.cfg.set("appearance_mode", mode)
        self.cfg.save()

    def save_advanced_settings(self):
        dpi_val = 144
        if "100" in self.seg_dpi.get():
            dpi_val = 100
        elif "200" in self.seg_dpi.get():
            dpi_val = 200

        self.cfg.set("dpi", dpi_val)
        self.cfg.set("grayscale", bool(self.switch_gray.get()))
        self.cfg.set("draw_border", bool(self.switch_border.get()))
        self.cfg.set("add_cover", bool(self.switch_cover.get()))
        self.cfg.set("add_end", bool(self.switch_end.get()))
        self.cfg.save()
        messagebox.showinfo("Başarılı", "Görsel ve kalite tercihleri kaydedildi!")

    # -------------------------------------------------------------
    # TAB 4: ABOUT & GITHUB
    # -------------------------------------------------------------
    def setup_about_tab(self):
        parent = self.tab_about

        box = ctk.CTkFrame(parent, fg_color="#1E293B", corner_radius=12)
        box.pack(fill="both", expand=True, padx=20, pady=20)

        ctk.CTkLabel(box, text="🎓 PoliTo Slayt Not Formatlayıcı", font=ctk.CTkFont(size=20, weight="bold")).pack(pady=(24, 6))
        ctk.CTkLabel(box, text="Sürüm 1.0.0 (Açık Kaynak)", font=ctk.CTkFont(size=13), text_color="#38BDF8").pack(pady=(0, 16))

        desc = (
            "Bu yazılım, üniversite slayt sunumlarını (PDF) sağ tarafında geniş not alma alanı olan,\n"
            "resmi üniversite armalı kapak ve bitiş sayfalarına sahip, yüksek kaliteli A4 fotokopi\n"
            "formatına dönüştürmek için tasarlanmıştır.\n\n"
            "Öne Çıkan Özellikler:\n"
            "• Tek tıkla toplu PDF işleme ve klasör tarama\n"
            "• Özel veya Politecnico di Torino logosu desteği\n"
            "• Kalıcı kullanıcı profili ve ders adı algılama\n"
            "• Çevrimdışı ve gizlilik odaklı (hiçbir veri dışarı aktarılmaz)\n"
            "• Vektörel netlikte 144 DPI baskı optimizasyonu"
        )
        ctk.CTkLabel(box, text=desc, font=ctk.CTkFont(size=13), justify="center").pack(pady=12)

        author_box = ctk.CTkFrame(box, fg_color="#0F172A", corner_radius=8)
        author_box.pack(pady=16, padx=30, fill="x")

        ctk.CTkLabel(author_box, text="Geliştirici: Haluk Tanrıseven (@haluked)", font=ctk.CTkFont(size=12, weight="bold")).pack(pady=(8, 2))
        ctk.CTkLabel(author_box, text="Politecnico di Torino - Lisans: MIT License", font=ctk.CTkFont(size=11), text_color="gray").pack(pady=(0, 8))

        ctk.CTkButton(
            box,
            text="🌐 GitHub Sayfasını Ziyaret Et",
            font=ctk.CTkFont(weight="bold"),
            command=lambda: webbrowser.open("https://github.com/haluked/slide-note-formatter"),
            width=220,
            height=36,
            fg_color="#3B82F6"
        ).pack(pady=(10, 20))

    # -------------------------------------------------------------
    # CONVERSION ACTIONS & LOGIC
    # -------------------------------------------------------------
    def on_preset_selected(self, choice):
        if "3 Slayt" in choice:
            self.cfg.set("slides_per_page", 3)
        elif "2 Slayt" in choice:
            self.cfg.set("slides_per_page", 2)
        elif "4 Slayt" in choice:
            self.cfg.set("slides_per_page", 4)

        if "Kapaksız" in choice:
            self.cfg.set("add_cover", False)
            self.cfg.set("add_end", False)
        else:
            self.cfg.set("add_cover", True)
            self.cfg.set("add_end", True)

        self.cfg.set("default_preset", choice)
        self.cfg.save()

    def on_dest_mode_changed(self):
        if self.dest_mode_var.get() == "custom":
            self.ent_dest_path.configure(state="normal")
            self.btn_browse_dest.configure(state="normal")
        else:
            self.ent_dest_path.configure(state="disabled")
            self.btn_browse_dest.configure(state="disabled")

    def browse_custom_dest(self):
        folder = filedialog.askdirectory(title="Çıktı Klasörünü Seçin")
        if folder:
            self.ent_dest_path.configure(state="normal")
            self.ent_dest_path.delete(0, "end")
            self.ent_dest_path.insert(0, os.path.normpath(folder))
            self.ent_dest_path.configure(state="disabled")
            self.cfg.set("custom_dest_folder", folder)
            self.cfg.save()

    def add_files(self):
        files = filedialog.askopenfilenames(
            title="Dönüştürülecek PDF Dosyalarını Seçin",
            filetypes=[("PDF Dosyaları", "*.pdf")]
        )
        if files:
            for f in files:
                norm_f = os.path.normpath(f)
                if norm_f not in self.selected_files and "(Not Alanli)" not in norm_f:
                    self.selected_files.append(norm_f)
            self.auto_detect_course()
            self.update_file_list()

    def add_folder(self):
        folder = filedialog.askdirectory(title="PDF İçeren Klasörü Seçin")
        if folder:
            for root, _, files in os.walk(folder):
                for f in files:
                    if f.lower().endswith(".pdf") and "(Not Alanli)" not in f:
                        norm_f = os.path.normpath(os.path.join(root, f))
                        if norm_f not in self.selected_files:
                            self.selected_files.append(norm_f)
            self.auto_detect_course()
            self.update_file_list()

    def auto_detect_course(self):
        if self.selected_files:
            detected = detect_course_name(self.selected_files)
            if detected != "COURSE":
                self.ent_course.delete(0, "end")
                self.ent_course.insert(0, detected)

    def clear_files(self):
        self.selected_files = []
        self.update_file_list()

    def update_file_list(self):
        self.file_textbox.configure(state="normal")
        self.file_textbox.delete("1.0", "end")
        if not self.selected_files:
            self.file_textbox.insert("1.0", "Henüz dosya seçilmedi.\nYukarıdaki butonlarla bir veya birden fazla PDF ekleyin...")
            self.lbl_file_count.configure(text="Toplam: 0 dosya seçildi")
        else:
            for idx, f in enumerate(self.selected_files, 1):
                self.file_textbox.insert("end", f"{idx}. {os.path.basename(f)}\n   ↳ {os.path.dirname(f)}\n")
            self.lbl_file_count.configure(text=f"Toplam: {len(self.selected_files)} dosya seçildi")
        self.file_textbox.configure(state="disabled")

    def start_conversion(self):
        if not self.selected_files:
            messagebox.showwarning("Uyarı", "Lütfen önce en az bir PDF dosyası seçin!")
            return

        if self.is_processing:
            return

        self.is_processing = True
        self.btn_start.configure(state="disabled")
        self.btn_add_files.configure(state="disabled")
        self.btn_add_folder.configure(state="disabled")
        self.btn_clear.configure(state="disabled")
        self.btn_open_folder.configure(state="disabled")

        thread = threading.Thread(target=self.run_batch_conversion, daemon=True)
        thread.start()

    def run_batch_conversion(self):
        total_files = len(self.selected_files)
        success_count = 0
        errors = []

        course = self.ent_course.get().strip()
        dest_mode = self.dest_mode_var.get()
        custom_dest = self.ent_dest_path.get().strip()

        # Load active logo
        logo_bytes = get_logo_bytes(self.cfg.get("custom_logo_path"))

        for file_idx, pdf_path in enumerate(self.selected_files):
            try:
                base_dir = os.path.dirname(pdf_path)
                file_name = os.path.basename(pdf_path)
                name_without_ext = os.path.splitext(file_name)[0]

                if dest_mode == "custom" and custom_dest and os.path.isdir(custom_dest):
                    target_dir = custom_dest
                else:
                    target_dir = os.path.join(base_dir, "_Not_Formatli")

                os.makedirs(target_dir, exist_ok=True)
                self.last_output_dir = target_dir

                s_count = self.cfg.get("slides_per_page", 3)
                out_filename = f"{name_without_ext} ({s_count} Slayt Not Alanli).pdf"
                out_path = os.path.join(target_dir, out_filename)

                def p_callback(cur_slide, total_s):
                    sub_prog = (file_idx + (cur_slide / max(total_s, 1))) / total_files
                    self.update_status(f"İşleniyor ({file_idx+1}/{total_files}): {file_name} [{cur_slide}/{total_s}]", sub_prog)

                process_pdf(
                    input_path=pdf_path,
                    output_path=out_path,
                    config=self.cfg.config,
                    course_name=course,
                    progress_callback=p_callback,
                    logo_bytes=logo_bytes
                )
                success_count += 1

            except Exception as e:
                err_detail = traceback.format_exc()
                errors.append(f"{os.path.basename(pdf_path)}: {str(e)}")
                print(f"Error ({pdf_path}): {err_detail}")

        if errors:
            err_summary = "\n".join(errors[:4])
            if len(errors) > 4:
                err_summary += f"\n... ve {len(errors)-4} dosya daha."
            self.after(0, lambda: messagebox.showerror("İşlem Uyarısı", f"Bazı dosyalar işlenirken hata oluştu:\n\n{err_summary}"))
            self.update_status(f"⚠️ {success_count}/{total_files} dosya tamamlandı, bazı hatalar oluştu.", 1.0)
        else:
            self.update_status(f"✅ Tamamlandı! {success_count}/{total_files} dosya başarıyla oluşturuldu.", 1.0)

        self.after(0, lambda: self.on_conversion_finished(success_count, total_files))

    def on_conversion_finished(self, success_count, total_files):
        self.is_processing = False
        self.btn_start.configure(state="normal")
        self.btn_add_files.configure(state="normal")
        self.btn_add_folder.configure(state="normal")
        self.btn_clear.configure(state="normal")
        if self.last_output_dir and os.path.isdir(self.last_output_dir):
            self.btn_open_folder.configure(state="normal")

        if success_count > 0:
            messagebox.showinfo(
                "İşlem Tamamlandı",
                f"Tebrikler! {success_count}/{total_files} PDF başarıyla dönüştürüldü!\n\n"
                f"Kayıt Konumu:\n{self.last_output_dir}\n\n"
                "'Çıktı Klasörünü Aç' butonuna basarak dosyalara ulaşabilirsiniz."
            )

    def open_output_folder(self):
        if self.last_output_dir and os.path.isdir(self.last_output_dir):
            os.startfile(self.last_output_dir)

    def update_status(self, text, progress_val):
        self.after(0, lambda: self.lbl_status.configure(text=text))
        self.after(0, lambda: self.progress_bar.set(progress_val))


def main():
    app = SlideNoteApp()
    app.mainloop()


if __name__ == "__main__":
    main()
