import os
import sys
import threading
import traceback
import webbrowser
import tkinter as tk
from tkinter import filedialog, messagebox
import customtkinter as ctk

# UTF-8 encoding support
if sys.stdout and hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

from config import ConfigManager
from pdf_engine import process_pdf
from assets import get_logo_bytes, get_app_icon_path


def detect_course_name(paths):
    if not paths:
        return ""
    import re
    # 1. Try from parent directory if it's a specific subject/course name
    parent_dirs = set()
    for p in paths:
        pdir = os.path.basename(os.path.dirname(os.path.normpath(p)))
        if pdir:
            parent_dirs.add(pdir)

    generic_names = {
        "desktop", "downloads", "documents", "belgeler", "masaüstü", "indirilenler",
        "pdf", "pdfs", "slayt", "slaytlar", "slides", "notes", "notlar", "ders", "dersler",
        "_not_formatli", "temp", "tmp", "output"
    }

    if len(parent_dirs) == 1:
        cand = list(parent_dirs)[0].strip()
        if cand.lower() not in generic_names and len(cand) > 2:
            return cand.upper()

    # 2. Try to parse from filename (e.g. MATH101, CS50, ELEC201)
    for p in paths:
        fname = os.path.splitext(os.path.basename(p))[0]
        code_match = re.search(r"\b([A-Za-z]{2,5}\s*\d{2,4}[A-Za-z]?)\b", fname)
        if code_match:
            return code_match.group(1).upper()

    return ""


# Unslop Theme Tokens (Light, Dark)
THEME = {
    "bg": ("#F8F9FA", "#121316"),
    "card": ("#FFFFFF", "#18191E"),
    "card_border": ("#E2E5E9", "#282A33"),
    "raised": ("#F1F3F5", "#21232B"),
    "raised_hover": ("#E9ECEF", "#2A2D37"),
    "border": ("#D8DCE3", "#2C2E38"),
    "text_primary": ("#16181D", "#EDEDF0"),
    "text_secondary": ("#505565", "#9DA3AF"),
    "text_muted": ("#858B98", "#656B77"),
    "accent": ("#2563EB", "#3B82F6"),
    "accent_hover": ("#1D4ED8", "#2563EB"),
    "primary_btn_bg": ("#16181D", "#F1F5F9"),
    "primary_btn_fg": ("#FFFFFF", "#0F172A"),
    "primary_btn_hover": ("#2B2E36", "#E2E8F0"),
}

PRESET_CHOICES = [
    "3 Slayt (Kapaklı)",
    "2 Slayt (Kapaklı)",
    "3 Slayt (Kapaksız)",
    "2 Slayt (Kapaksız)",
    "4 Slayt (Kompakt)",
]


class SlideNoteApp(ctk.CTk):
    def __init__(self):
        super().__init__()

        self.cfg = ConfigManager()

        self.title("Slayt Not Formatlayıcı")
        self.geometry("1040, 750")
        self.minsize(960, 680)

        # Apply appearance mode
        saved_mode = self.cfg.get("appearance_mode", "dark")
        ctk.set_appearance_mode(saved_mode)
        ctk.set_default_color_theme("blue")

        self.configure(fg_color=THEME["bg"])

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

    def get_profile_tag_text(self):
        user = str(self.cfg.get("user_name", "")).strip()
        uni = str(self.cfg.get("university_name", "")).strip()
        if user and uni:
            return f"{user}  ·  {uni}"
        elif user:
            return user
        elif uni:
            return uni
        return "Öğrenci Profili (Ayarlanmadı)"

    def setup_ui(self):
        # 1. Top Header Bar
        header = ctk.CTkFrame(self, fg_color="transparent")
        header.pack(fill="x", padx=24, pady=(16, 12))

        header_left = ctk.CTkFrame(header, fg_color="transparent")
        header_left.pack(side="left")

        title_lbl = ctk.CTkLabel(
            header_left,
            text="Slayt Not Formatlayıcı",
            font=ctk.CTkFont(size=18, weight="bold"),
            text_color=THEME["text_primary"]
        )
        title_lbl.pack(side="left")

        ver_badge = ctk.CTkFrame(
            header_left,
            fg_color=THEME["raised"],
            corner_radius=4,
            border_width=1,
            border_color=THEME["card_border"]
        )
        ver_badge.pack(side="left", padx=(10, 0))

        ctk.CTkLabel(
            ver_badge,
            text="v1.1",
            font=ctk.CTkFont(family="Consolas", size=10),
            text_color=THEME["text_muted"]
        ).pack(padx=6, pady=2)

        self.lbl_profile_tag = ctk.CTkLabel(
            header,
            text=self.get_profile_tag_text(),
            font=ctk.CTkFont(size=12),
            text_color=THEME["text_muted"]
        )
        self.lbl_profile_tag.pack(side="right")

        # 2. Main Tab View
        self.tabview = ctk.CTkTabview(
            self,
            corner_radius=8,
            fg_color=THEME["card"],
            border_width=1,
            border_color=THEME["card_border"],
            segmented_button_fg_color=THEME["raised"],
            segmented_button_selected_color=THEME["accent"],
            segmented_button_selected_hover_color=THEME["accent_hover"],
            segmented_button_unselected_color=THEME["raised"],
            segmented_button_unselected_hover_color=THEME["raised_hover"],
            text_color=THEME["text_primary"]
        )
        self.tabview.pack(fill="both", expand=True, padx=24, pady=(0, 16))

        self.tab_convert = self.tabview.add("Dönüştürücü")
        self.tab_profile = self.tabview.add("Profil & Logo")
        self.tab_settings = self.tabview.add("Baskı & Çıktı")
        self.tab_about = self.tabview.add("Hakkında")

        self.setup_converter_tab()
        self.setup_profile_tab()
        self.setup_settings_tab()
        self.setup_about_tab()

    # -------------------------------------------------------------
    # TAB 1: CONVERTER
    # -------------------------------------------------------------
    def setup_converter_tab(self):
        parent = self.tab_convert

        # Bottom Action Bar (Packed to bottom first to ensure it's always docked and visible)
        bottom_box = ctk.CTkFrame(parent, fg_color="transparent")
        bottom_box.pack(side="bottom", fill="x", padx=8, pady=(8, 4))

        self.progress_bar = ctk.CTkProgressBar(
            bottom_box,
            height=4,
            progress_color=THEME["accent"][1],
            fg_color=THEME["raised"][1]
        )
        self.progress_bar.pack(fill="x", pady=(0, 8))
        self.progress_bar.set(0)

        action_row = ctk.CTkFrame(bottom_box, fg_color="transparent")
        action_row.pack(fill="x")

        self.lbl_status = ctk.CTkLabel(
            action_row,
            text="Hazır",
            font=ctk.CTkFont(size=12),
            text_color=THEME["text_secondary"]
        )
        self.lbl_status.pack(side="left")

        self.btn_start = ctk.CTkButton(
            action_row,
            text="Dönüştürmeyi Başlat",
            font=ctk.CTkFont(size=13, weight="bold"),
            command=self.start_conversion,
            width=180,
            height=38,
            fg_color=THEME["primary_btn_bg"],
            hover_color=THEME["primary_btn_hover"],
            text_color=THEME["primary_btn_fg"],
            corner_radius=6
        )
        self.btn_start.pack(side="right")

        self.btn_open_folder = ctk.CTkButton(
            action_row,
            text="Çıktı Klasörünü Aç",
            command=self.open_output_folder,
            width=150,
            height=38,
            fg_color=THEME["raised"],
            hover_color=THEME["raised_hover"],
            text_color=THEME["text_primary"],
            border_width=1,
            border_color=THEME["border"],
            corner_radius=6,
            state="disabled"
        )
        self.btn_open_folder.pack(side="right", padx=(0, 10))

        main_split = ctk.CTkFrame(parent, fg_color="transparent")
        main_split.pack(fill="both", expand=True, padx=8, pady=(4, 8))

        # LEFT COLUMN: File Queue Management
        left_col = ctk.CTkFrame(
            main_split,
            fg_color=THEME["card"],
            corner_radius=8,
            border_width=1,
            border_color=THEME["card_border"]
        )
        left_col.pack(side="left", fill="both", expand=True, padx=(0, 8))

        # Left Header
        left_header = ctk.CTkFrame(left_col, fg_color="transparent")
        left_header.pack(fill="x", padx=16, pady=(14, 8))

        ctk.CTkLabel(
            left_header,
            text="Slayt Dosyaları",
            font=ctk.CTkFont(size=14, weight="bold"),
            text_color=THEME["text_primary"]
        ).pack(side="left")

        self.lbl_file_count = ctk.CTkLabel(
            left_header,
            text="0 dosya",
            font=ctk.CTkFont(family="Consolas", size=11),
            text_color=THEME["text_muted"]
        )
        self.lbl_file_count.pack(side="left", padx=(8, 0))

        self.btn_clear = ctk.CTkButton(
            left_header,
            text="Temizle",
            command=self.clear_files,
            width=64,
            height=26,
            fg_color="transparent",
            hover_color=THEME["raised_hover"],
            text_color=THEME["text_muted"],
            font=ctk.CTkFont(size=11)
        )
        self.btn_clear.pack(side="right")

        # Action Buttons Row
        btn_row = ctk.CTkFrame(left_col, fg_color="transparent")
        btn_row.pack(fill="x", padx=16, pady=(0, 10))

        self.btn_add_files = ctk.CTkButton(
            btn_row,
            text="+ Dosya Ekle",
            command=self.add_files,
            width=110,
            height=32,
            fg_color=THEME["raised"],
            hover_color=THEME["raised_hover"],
            text_color=THEME["text_primary"],
            border_width=1,
            border_color=THEME["border"],
            font=ctk.CTkFont(size=12, weight="bold")
        )
        self.btn_add_files.pack(side="left", padx=(0, 8))

        self.btn_add_folder = ctk.CTkButton(
            btn_row,
            text="Klasör Tara",
            command=self.add_folder,
            width=110,
            height=32,
            fg_color=THEME["raised"],
            hover_color=THEME["raised_hover"],
            text_color=THEME["text_primary"],
            border_width=1,
            border_color=THEME["border"],
            font=ctk.CTkFont(size=12)
        )
        self.btn_add_folder.pack(side="left")

        # Scrollable File List Container
        self.file_list_frame = ctk.CTkScrollableFrame(
            left_col,
            fg_color=THEME["bg"],
            corner_radius=6,
            border_width=1,
            border_color=THEME["card_border"]
        )
        self.file_list_frame.pack(fill="both", expand=True, padx=16, pady=(0, 14))

        self.update_file_list()

        # RIGHT COLUMN: Model Options, Live Layout Preview & Destination
        right_col = ctk.CTkScrollableFrame(
            main_split,
            fg_color=THEME["card"],
            corner_radius=8,
            border_width=1,
            border_color=THEME["card_border"]
        )
        right_col.pack(side="right", fill="both", expand=True, padx=(8, 0))

        # Section 1: Template & Course
        ctk.CTkLabel(
            right_col,
            text="Şablon & Ders Bilgisi",
            font=ctk.CTkFont(size=13, weight="bold"),
            text_color=THEME["text_primary"]
        ).pack(anchor="w", padx=12, pady=(12, 6))

        preset_box = ctk.CTkFrame(right_col, fg_color=THEME["raised"], corner_radius=6)
        preset_box.pack(fill="x", padx=12, pady=(0, 12))

        ctk.CTkLabel(
            preset_box,
            text="Sayfa Düzeni Şablonu",
            font=ctk.CTkFont(size=11),
            text_color=THEME["text_secondary"]
        ).pack(anchor="w", padx=12, pady=(8, 2))

        cur_preset = self.normalize_preset_name(self.cfg.get("default_preset"))
        self.preset_var = ctk.StringVar(value=cur_preset)
        self.preset_menu = ctk.CTkOptionMenu(
            preset_box,
            values=PRESET_CHOICES,
            variable=self.preset_var,
            command=self.on_preset_selected,
            fg_color=THEME["card"],
            button_color=THEME["border"],
            button_hover_color=THEME["accent"],
            text_color=THEME["text_primary"],
            dropdown_fg_color=THEME["card"],
            dropdown_text_color=THEME["text_primary"],
            dropdown_hover_color=THEME["raised_hover"],
            height=32
        )
        self.preset_menu.pack(fill="x", padx=12, pady=(0, 8))

        ctk.CTkLabel(
            preset_box,
            text="Ders Adı (Kapak Sayfası)",
            font=ctk.CTkFont(size=11),
            text_color=THEME["text_secondary"]
        ).pack(anchor="w", padx=12, pady=(0, 2))

        self.ent_course = ctk.CTkEntry(
            preset_box,
            placeholder_text="Ders adı (otomatik algılanır veya yazın)",
            fg_color=THEME["card"],
            border_color=THEME["border"],
            text_color=THEME["text_primary"],
            height=32
        )
        self.ent_course.pack(fill="x", padx=12, pady=(0, 10))
        self.ent_course.bind("<KeyRelease>", lambda e: self.update_preview_canvas())

        # Section 2: Live A4 Layout Preview (Craft Moment)
        ctk.CTkLabel(
            right_col,
            text="Canlı A4 Sayfa Önizlemesi",
            font=ctk.CTkFont(size=13, weight="bold"),
            text_color=THEME["text_primary"]
        ).pack(anchor="w", padx=12, pady=(4, 6))

        preview_card = ctk.CTkFrame(right_col, fg_color=THEME["raised"], corner_radius=6)
        preview_card.pack(fill="x", padx=12, pady=(0, 12))

        preview_inner = ctk.CTkFrame(preview_card, fg_color="transparent")
        preview_inner.pack(fill="x", padx=12, pady=12)

        # Tkinter Canvas representing A4 Sheet (ratio 1 : 1.414)
        self.canvas_w = 124
        self.canvas_h = 175
        self.preview_canvas = tk.Canvas(
            preview_inner,
            width=self.canvas_w,
            height=self.canvas_h,
            bg="#FFFFFF",
            highlightthickness=1,
            highlightbackground="#CBD5E1"
        )
        self.preview_canvas.pack(side="left", padx=(0, 14))

        # Technical Specification Details beside canvas
        self.specs_frame = ctk.CTkFrame(preview_inner, fg_color="transparent")
        self.specs_frame.pack(side="left", fill="both", expand=True)

        self.lbl_spec_format = ctk.CTkLabel(
            self.specs_frame,
            text="Format: A4 Dikey (210×297 mm)",
            font=ctk.CTkFont(size=11),
            text_color=THEME["text_primary"]
        )
        self.lbl_spec_format.pack(anchor="w", pady=(0, 3))

        self.lbl_spec_layout = ctk.CTkLabel(
            self.specs_frame,
            text="Düzen: 3 Slayt + Sağ Not Alanı",
            font=ctk.CTkFont(size=11),
            text_color=THEME["text_secondary"]
        )
        self.lbl_spec_layout.pack(anchor="w", pady=3)

        self.lbl_spec_cover = ctk.CTkLabel(
            self.specs_frame,
            text="Kapak: Kurum Arması + Başlık",
            font=ctk.CTkFont(size=11),
            text_color=THEME["text_secondary"]
        )
        self.lbl_spec_cover.pack(anchor="w", pady=3)

        self.lbl_spec_print = ctk.CTkLabel(
            self.specs_frame,
            text="Baskı: Gri Tonlama / 144 DPI",
            font=ctk.CTkFont(size=11),
            text_color=THEME["text_secondary"]
        )
        self.lbl_spec_print.pack(anchor="w", pady=3)

        cur_user = str(self.cfg.get('user_name', '')).strip()
        self.lbl_spec_author = ctk.CTkLabel(
            self.specs_frame,
            text=f"Filigran: {cur_user}" if cur_user else "Filigran: Belirtilmedi",
            font=ctk.CTkFont(size=11),
            text_color=THEME["text_muted"]
        )
        self.lbl_spec_author.pack(anchor="w", pady=(3, 0))

        # Section 3: Output Destination
        ctk.CTkLabel(
            right_col,
            text="Kayıt Konumu",
            font=ctk.CTkFont(size=13, weight="bold"),
            text_color=THEME["text_primary"]
        ).pack(anchor="w", padx=12, pady=(4, 6))

        dest_box = ctk.CTkFrame(right_col, fg_color=THEME["raised"], corner_radius=6)
        dest_box.pack(fill="x", padx=12, pady=(0, 12))

        self.dest_mode_var = ctk.StringVar(value=self.cfg.get("dest_mode", "subfolder"))
        self.rb_sub = ctk.CTkRadioButton(
            dest_box,
            text="Kaynak klasörde alt klasöre kaydet (_Not_Formatli/)",
            variable=self.dest_mode_var,
            value="subfolder",
            command=self.on_dest_mode_changed,
            text_color=THEME["text_primary"],
            fg_color=THEME["accent"],
            border_color=THEME["border"],
            font=ctk.CTkFont(size=12)
        )
        self.rb_sub.pack(anchor="w", padx=12, pady=(10, 4))

        self.rb_cust = ctk.CTkRadioButton(
            dest_box,
            text="Özel bir hedef klasöre kaydet:",
            variable=self.dest_mode_var,
            value="custom",
            command=self.on_dest_mode_changed,
            text_color=THEME["text_primary"],
            fg_color=THEME["accent"],
            border_color=THEME["border"],
            font=ctk.CTkFont(size=12)
        )
        self.rb_cust.pack(anchor="w", padx=12, pady=4)

        cust_row = ctk.CTkFrame(dest_box, fg_color="transparent")
        cust_row.pack(fill="x", padx=12, pady=(2, 10))

        self.ent_dest_path = ctk.CTkEntry(
            cust_row,
            placeholder_text="Klasör seçilmedi",
            fg_color=THEME["card"],
            border_color=THEME["border"],
            text_color=THEME["text_primary"],
            height=32,
            state="disabled"
        )
        self.ent_dest_path.pack(side="left", fill="x", expand=True, padx=(0, 8))

        saved_dest = self.cfg.get("custom_dest_folder", "")
        if saved_dest:
            self.ent_dest_path.configure(state="normal")
            self.ent_dest_path.insert(0, saved_dest)
            self.ent_dest_path.configure(state="disabled")

        self.btn_browse_dest = ctk.CTkButton(
            cust_row,
            text="Gözat...",
            width=70,
            height=32,
            command=self.browse_custom_dest,
            fg_color=THEME["card"],
            hover_color=THEME["raised_hover"],
            text_color=THEME["text_primary"],
            border_width=1,
            border_color=THEME["border"],
            state="disabled"
        )
        self.btn_browse_dest.pack(side="right")

        self.on_dest_mode_changed()

        # Initial Preview Render
        self.update_preview_canvas()

    def normalize_preset_name(self, raw_preset):
        if not raw_preset:
            return PRESET_CHOICES[0]
        if "3 Slayt" in raw_preset and "Kapak" in raw_preset and "Kapaksız" not in raw_preset:
            return "3 Slayt (Kapaklı)"
        elif "2 Slayt" in raw_preset and "Kapak" in raw_preset and "Kapaksız" not in raw_preset:
            return "2 Slayt (Kapaklı)"
        elif "3 Slayt" in raw_preset and "Kapaksız" in raw_preset:
            return "3 Slayt (Kapaksız)"
        elif "2 Slayt" in raw_preset and "Kapaksız" in raw_preset:
            return "2 Slayt (Kapaksız)"
        elif "4 Slayt" in raw_preset:
            return "4 Slayt (Kompakt)"
        return PRESET_CHOICES[0]

    # -------------------------------------------------------------
    # LIVE A4 PREVIEW CANVAS REDRAW
    # -------------------------------------------------------------
    def update_preview_canvas(self):
        self.preview_canvas.delete("all")
        w, h = self.canvas_w, self.canvas_h

        # Draw sheet boundary & drop shadow
        self.preview_canvas.create_rectangle(0, 0, w, h, fill="#FFFFFF", outline="#D1D5DB", width=1)

        choice = self.preset_var.get()
        slides = 3
        has_cover = True
        if "2 Slayt" in choice:
            slides = 2
        elif "4 Slayt" in choice:
            slides = 4

        if "Kapaksız" in choice:
            has_cover = False

        # Header Watermark
        user_name = self.cfg.get("user_name", "")
        if user_name:
            self.preview_canvas.create_text(
                w - 8, 8,
                text=user_name,
                fill="#94A3B8",
                font=("Arial", 6),
                anchor="ne"
            )

        # Draw Slide slots & ruled note lines
        margin_x = 8
        slide_w = 58
        note_x0 = margin_x + slide_w + 5
        note_x1 = w - 8

        slot_h = (h - 18) / slides

        for i in range(slides):
            slot_top = 14 + i * slot_h
            if slides == 2:
                slide_h = 42
                line_count = 6
            elif slides == 4:
                slide_h = 24
                line_count = 3
            else:
                slide_h = 32
                line_count = 4

            slide_y = slot_top + (slot_h - slide_h) / 2.0

            # Slide Box
            self.preview_canvas.create_rectangle(
                margin_x, slide_y, margin_x + slide_w, slide_y + slide_h,
                fill="#F1F5F9", outline="#94A3B8", width=1
            )
            # Mini slide mock content (title bar + bullet marks)
            self.preview_canvas.create_rectangle(
                margin_x + 3, slide_y + 3, margin_x + slide_w - 3, slide_y + 7,
                fill="#CBD5E1", outline=""
            )
            self.preview_canvas.create_line(
                margin_x + 5, slide_y + 11, margin_x + slide_w - 8, slide_y + 11,
                fill="#E2E8F0"
            )
            self.preview_canvas.create_line(
                margin_x + 5, slide_y + 15, margin_x + slide_w - 14, slide_y + 15,
                fill="#E2E8F0"
            )

            # Ruled Note Lines on the right
            line_spacing = slide_h / float(line_count)
            for li in range(1, line_count + 1):
                ly = slide_y + li * line_spacing - 2
                self.preview_canvas.create_line(
                    note_x0, ly, note_x1, ly,
                    fill="#E2E8F0", width=1
                )

        # Update text labels beside preview
        self.lbl_spec_layout.configure(text=f"Düzen: {slides} Slayt + Sağ Not Alanı")
        if has_cover:
            cpath = self.cfg.get("custom_logo_path", "")
            if cpath and os.path.isfile(cpath):
                cover_text = "Var (Özel Logo + Başlık)"
            else:
                cover_text = "Var (Kurum Arması + Başlık)"
        else:
            cover_text = "Kapaksız (Doğrudan Not Sayfaları)"
        self.lbl_spec_cover.configure(text=f"Kapak: {cover_text}")

        user = str(self.cfg.get("user_name", "")).strip()
        self.lbl_spec_author.configure(text=f"Filigran: {user}" if user else "Filigran: Belirtilmedi")

        dpi_val = self.cfg.get("dpi", 144)
        is_gray = self.cfg.get("grayscale", True)
        color_str = "Gri Tonlama" if is_gray else "Tam Renkli"
        self.lbl_spec_print.configure(text=f"Baskı: {color_str} / {dpi_val} DPI")

    # -------------------------------------------------------------
    # TAB 2: PROFILE & UNIVERSITY
    # -------------------------------------------------------------
    def setup_profile_tab(self):
        parent = self.tab_profile

        scroll = ctk.CTkScrollableFrame(parent, fg_color="transparent")
        scroll.pack(fill="both", expand=True, padx=16, pady=16)

        # Card 1: User & University Details
        card_user = ctk.CTkFrame(
            scroll,
            fg_color=THEME["card"],
            corner_radius=8,
            border_width=1,
            border_color=THEME["card_border"]
        )
        card_user.pack(fill="x", pady=(0, 16))

        ctk.CTkLabel(
            card_user,
            text="Öğrenci & Kurum Bilgileri",
            font=ctk.CTkFont(size=14, weight="bold"),
            text_color=THEME["text_primary"]
        ).pack(anchor="w", padx=16, pady=(16, 4))

        ctk.CTkLabel(
            card_user,
            text="Bu bilgiler kapak sayfası, bitiş sayfası ve üst bilgi filigranında kullanılır.",
            font=ctk.CTkFont(size=12),
            text_color=THEME["text_secondary"]
        ).pack(anchor="w", padx=16, pady=(0, 16))

        # User Name
        ctk.CTkLabel(
            card_user,
            text="Öğrenci / Kullanıcı Adı (Filigran & Kapak)",
            font=ctk.CTkFont(size=12, weight="bold"),
            text_color=THEME["text_primary"]
        ).pack(anchor="w", padx=16, pady=(0, 4))

        self.ent_prof_name = ctk.CTkEntry(
            card_user,
            placeholder_text="Örn: Ad Soyad",
            width=420,
            height=34,
            fg_color=THEME["raised"],
            border_color=THEME["border"],
            text_color=THEME["text_primary"]
        )
        saved_name = self.cfg.get("user_name", "")
        if saved_name:
            self.ent_prof_name.insert(0, saved_name)
        self.ent_prof_name.pack(anchor="w", padx=16, pady=(0, 12))

        # University Name
        ctk.CTkLabel(
            card_user,
            text="Üniversite / Kurum Adı (Kapak)",
            font=ctk.CTkFont(size=12, weight="bold"),
            text_color=THEME["text_primary"]
        ).pack(anchor="w", padx=16, pady=(0, 4))

        self.ent_prof_uni = ctk.CTkEntry(
            card_user,
            placeholder_text="Örn: Üniversite veya Okul Adı",
            width=420,
            height=34,
            fg_color=THEME["raised"],
            border_color=THEME["border"],
            text_color=THEME["text_primary"]
        )
        saved_uni = self.cfg.get("university_name", "")
        if saved_uni:
            self.ent_prof_uni.insert(0, saved_uni)
        self.ent_prof_uni.pack(anchor="w", padx=16, pady=(0, 12))

        # Subtitle
        ctk.CTkLabel(
            card_user,
            text="Kapak Alt Başlığı",
            font=ctk.CTkFont(size=12, weight="bold"),
            text_color=THEME["text_primary"]
        ).pack(anchor="w", padx=16, pady=(0, 4))

        self.ent_prof_sub = ctk.CTkEntry(
            card_user,
            placeholder_text="Örn: Lecture Notes & Study Handouts",
            width=420,
            height=34,
            fg_color=THEME["raised"],
            border_color=THEME["border"],
            text_color=THEME["text_primary"]
        )
        saved_sub = self.cfg.get("subtitle", "Lecture Notes & Study Handouts")
        if saved_sub:
            self.ent_prof_sub.insert(0, saved_sub)
        self.ent_prof_sub.pack(anchor="w", padx=16, pady=(0, 16))

        # Card 2: Logo Management
        card_logo = ctk.CTkFrame(
            scroll,
            fg_color=THEME["card"],
            corner_radius=8,
            border_width=1,
            border_color=THEME["card_border"]
        )
        card_logo.pack(fill="x", pady=(0, 16))

        ctk.CTkLabel(
            card_logo,
            text="Kapak Sayfası Logosu",
            font=ctk.CTkFont(size=14, weight="bold"),
            text_color=THEME["text_primary"]
        ).pack(anchor="w", padx=16, pady=(16, 4))

        self.lbl_active_logo = ctk.CTkLabel(
            card_logo,
            text=self.get_active_logo_label(),
            font=ctk.CTkFont(size=12),
            text_color=THEME["text_secondary"]
        )
        self.lbl_active_logo.pack(anchor="w", padx=16, pady=(0, 12))

        logo_btn_row = ctk.CTkFrame(card_logo, fg_color="transparent")
        logo_btn_row.pack(fill="x", padx=16, pady=(0, 16))

        ctk.CTkButton(
            logo_btn_row,
            text="Özel Logo Seç (PNG/JPG)...",
            command=self.select_custom_logo,
            height=34,
            fg_color=THEME["raised"],
            hover_color=THEME["raised_hover"],
            text_color=THEME["text_primary"],
            border_width=1,
            border_color=THEME["border"]
        ).pack(side="left", padx=(0, 8))

        ctk.CTkButton(
            logo_btn_row,
            text="Varsayılan Armaya Sıfırla",
            command=self.reset_to_default_logo,
            height=34,
            fg_color=THEME["raised"],
            hover_color=THEME["raised_hover"],
            text_color=THEME["text_primary"],
            border_width=1,
            border_color=THEME["border"]
        ).pack(side="left")

        # Save Action
        ctk.CTkButton(
            scroll,
            text="Profil Bilgilerini Kaydet",
            font=ctk.CTkFont(size=13, weight="bold"),
            height=38,
            width=200,
            fg_color=THEME["primary_btn_bg"],
            hover_color=THEME["primary_btn_hover"],
            text_color=THEME["primary_btn_fg"],
            corner_radius=6,
            command=self.save_profile_settings
        ).pack(anchor="w", pady=(0, 16))

    def get_active_logo_label(self):
        cpath = self.cfg.get("custom_logo_path")
        if cpath and os.path.isfile(cpath):
            return f"Aktif Logo: Özel Görsel ({os.path.basename(cpath)})"
        return "Aktif Logo: Varsayılan Üniversite Arması (Dahili)"

    def select_custom_logo(self):
        file = filedialog.askopenfilename(
            title="Üniversite veya Kurum Logosu Seçin",
            filetypes=[("Görsel Dosyaları", "*.png *.jpg *.jpeg")]
        )
        if file:
            self.cfg.set("custom_logo_path", file)
            self.cfg.save()
            self.lbl_active_logo.configure(text=self.get_active_logo_label())
            self.update_preview_canvas()
            messagebox.showinfo("Logo Güncellendi", f"Özel logo seçildi:\n{os.path.basename(file)}")

    def reset_to_default_logo(self):
        self.cfg.set("custom_logo_path", "")
        self.cfg.save()
        self.lbl_active_logo.configure(text=self.get_active_logo_label())
        self.update_preview_canvas()
        messagebox.showinfo("Logo Sıfırlandı", "Varsayılan armaya dönüldü.")

    def save_profile_settings(self):
        self.cfg.set("user_name", self.ent_prof_name.get().strip())
        self.cfg.set("university_name", self.ent_prof_uni.get().strip())
        self.cfg.set("subtitle", self.ent_prof_sub.get().strip())
        self.cfg.save()

        self.lbl_profile_tag.configure(text=self.get_profile_tag_text())
        cur_user = str(self.cfg.get("user_name", "")).strip()
        self.lbl_spec_author.configure(text=f"Filigran: {cur_user}" if cur_user else "Filigran: Belirtilmedi")
        self.update_preview_canvas()
        messagebox.showinfo("Bilgi", "Profil ve kurum bilgileri kaydedildi.")
        self.cfg.set("university_name", self.ent_prof_uni.get().strip())
        self.cfg.set("subtitle", self.ent_prof_sub.get().strip())
        self.cfg.save()

        self.lbl_profile_tag.configure(text=f"{self.cfg.get('user_name')}  ·  {self.cfg.get('university_name')}")
        self.lbl_spec_author.configure(text=f"Filigran: {self.cfg.get('user_name')}")
        self.update_preview_canvas()
        messagebox.showinfo("Bilgi", "Profil ve üniversite ayarları kaydedildi.")

    # -------------------------------------------------------------
    # TAB 3: PRINT & OUTPUT SETTINGS
    # -------------------------------------------------------------
    def setup_settings_tab(self):
        parent = self.tab_settings

        scroll = ctk.CTkScrollableFrame(parent, fg_color="transparent")
        scroll.pack(fill="both", expand=True, padx=16, pady=16)

        # Card 1: Resolution & Quality
        card_res = ctk.CTkFrame(
            scroll,
            fg_color=THEME["card"],
            corner_radius=8,
            border_width=1,
            border_color=THEME["card_border"]
        )
        card_res.pack(fill="x", pady=(0, 16))

        ctk.CTkLabel(
            card_res,
            text="Baskı Kalitesi & Çözünürlük",
            font=ctk.CTkFont(size=14, weight="bold"),
            text_color=THEME["text_primary"]
        ).pack(anchor="w", padx=16, pady=(16, 4))

        ctk.CTkLabel(
            card_res,
            text="DPI değeri arttıkça slayt içerisindeki küçük yazılar daha net basılır; 144 DPI fotokopi için ideal dengedir.",
            font=ctk.CTkFont(size=12),
            text_color=THEME["text_secondary"]
        ).pack(anchor="w", padx=16, pady=(0, 12))

        self.seg_dpi = ctk.CTkSegmentedButton(
            card_res,
            values=["100 DPI (Kompakt)", "144 DPI (Önerilen)", "200 DPI (Yüksek Netlik)"],
            fg_color=THEME["raised"],
            selected_color=THEME["accent"],
            selected_hover_color=THEME["accent_hover"],
            unselected_color=THEME["raised"],
            unselected_hover_color=THEME["raised_hover"],
            text_color=THEME["text_primary"],
            height=32
        )
        current_dpi = self.cfg.get("dpi", 144)
        if current_dpi == 100:
            self.seg_dpi.set("100 DPI (Kompakt)")
        elif current_dpi == 200:
            self.seg_dpi.set("200 DPI (Yüksek Netlik)")
        else:
            self.seg_dpi.set("144 DPI (Önerilen)")
        self.seg_dpi.pack(anchor="w", padx=16, pady=(0, 16))

        # Card 2: Print Page Options
        card_opts = ctk.CTkFrame(
            scroll,
            fg_color=THEME["card"],
            corner_radius=8,
            border_width=1,
            border_color=THEME["card_border"]
        )
        card_opts.pack(fill="x", pady=(0, 16))

        ctk.CTkLabel(
            card_opts,
            text="Fotokopi & Sayfa Seçenekleri",
            font=ctk.CTkFont(size=14, weight="bold"),
            text_color=THEME["text_primary"]
        ).pack(anchor="w", padx=16, pady=(16, 4))

        self.switch_gray = ctk.CTkSwitch(
            card_opts,
            text="Gri tonlama (Siyah-beyaz fotokopi için toner tasarrufu)",
            text_color=THEME["text_primary"],
            fg_color=THEME["raised"],
            progress_color=THEME["accent"],
            font=ctk.CTkFont(size=12),
            command=self.update_preview_canvas
        )
        if self.cfg.get("grayscale", True):
            self.switch_gray.select()
        else:
            self.switch_gray.deselect()
        self.switch_gray.pack(anchor="w", padx=16, pady=(8, 6))

        self.switch_border = ctk.CTkSwitch(
            card_opts,
            text="Slayt etrafına ince gri çerçeve ekle",
            text_color=THEME["text_primary"],
            fg_color=THEME["raised"],
            progress_color=THEME["accent"],
            font=ctk.CTkFont(size=12)
        )
        if self.cfg.get("draw_border", False):
            self.switch_border.select()
        else:
            self.switch_border.deselect()
        self.switch_border.pack(anchor="w", padx=16, pady=6)

        self.switch_cover = ctk.CTkSwitch(
            card_opts,
            text="Ön kapak sayfası oluştur (Logo, ders ve modül başlığı)",
            text_color=THEME["text_primary"],
            fg_color=THEME["raised"],
            progress_color=THEME["accent"],
            font=ctk.CTkFont(size=12),
            command=self.update_preview_canvas
        )
        if self.cfg.get("add_cover", True):
            self.switch_cover.select()
        else:
            self.switch_cover.deselect()
        self.switch_cover.pack(anchor="w", padx=16, pady=6)

        self.switch_end = ctk.CTkSwitch(
            card_opts,
            text="Bitiş sayfası oluştur (- END OF MODULE -)",
            text_color=THEME["text_primary"],
            fg_color=THEME["raised"],
            progress_color=THEME["accent"],
            font=ctk.CTkFont(size=12)
        )
        if self.cfg.get("add_end", True):
            self.switch_end.select()
        else:
            self.switch_end.deselect()
        self.switch_end.pack(anchor="w", padx=16, pady=(6, 16))

        # Card 3: Interface Theme
        card_theme = ctk.CTkFrame(
            scroll,
            fg_color=THEME["card"],
            corner_radius=8,
            border_width=1,
            border_color=THEME["card_border"]
        )
        card_theme.pack(fill="x", pady=(0, 16))

        ctk.CTkLabel(
            card_theme,
            text="Arayüz Görünümü",
            font=ctk.CTkFont(size=14, weight="bold"),
            text_color=THEME["text_primary"]
        ).pack(anchor="w", padx=16, pady=(16, 4))

        self.seg_theme = ctk.CTkSegmentedButton(
            card_theme,
            values=["Karanlık", "Aydınlık", "Sistem"],
            command=self.on_theme_changed,
            fg_color=THEME["raised"],
            selected_color=THEME["accent"],
            selected_hover_color=THEME["accent_hover"],
            unselected_color=THEME["raised"],
            unselected_hover_color=THEME["raised_hover"],
            text_color=THEME["text_primary"],
            height=32
        )
        current_theme = self.cfg.get("appearance_mode", "dark")
        if current_theme == "light":
            self.seg_theme.set("Aydınlık")
        elif current_theme == "system":
            self.seg_theme.set("Sistem")
        else:
            self.seg_theme.set("Karanlık")
        self.seg_theme.pack(anchor="w", padx=16, pady=(0, 16))

        # Save Button
        ctk.CTkButton(
            scroll,
            text="Baskı Tercihlerini Kaydet",
            font=ctk.CTkFont(size=13, weight="bold"),
            height=38,
            width=200,
            fg_color=THEME["primary_btn_bg"],
            hover_color=THEME["primary_btn_hover"],
            text_color=THEME["primary_btn_fg"],
            corner_radius=6,
            command=self.save_advanced_settings
        ).pack(anchor="w", pady=(0, 16))

    def on_theme_changed(self, value):
        mode_map = {"Karanlık": "dark", "Aydınlık": "light", "Sistem": "system"}
        mode = mode_map.get(value, "dark")
        ctk.set_appearance_mode(mode)
        self.cfg.set("appearance_mode", mode)
        self.cfg.save()
        self.update_preview_canvas()

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
        self.update_preview_canvas()
        messagebox.showinfo("Bilgi", "Görsel ve baskı tercihleri kaydedildi.")

    # -------------------------------------------------------------
    # TAB 4: ABOUT
    # -------------------------------------------------------------
    def setup_about_tab(self):
        parent = self.tab_about

        card = ctk.CTkFrame(
            parent,
            fg_color=THEME["card"],
            corner_radius=8,
            border_width=1,
            border_color=THEME["card_border"]
        )
        card.pack(fill="both", expand=True, padx=16, pady=16)

        ctk.CTkLabel(
            card,
            text="Slayt Not Formatlayıcı",
            font=ctk.CTkFont(size=18, weight="bold"),
            text_color=THEME["text_primary"]
        ).pack(pady=(28, 4))

        ctk.CTkLabel(
            card,
            text="Sürüm 1.1 · Açık Kaynak",
            font=ctk.CTkFont(family="Consolas", size=12),
            text_color=THEME["text_muted"]
        ).pack(pady=(0, 16))

        desc = (
            "Ders slayt sunumlarını (PDF) sağ tarafında geniş not alma alanı bulunan,\n"
            "kurum armalı kapak ve bitiş sayfalarına sahip, yüksek kaliteli A4 fotokopi\n"
            "formatına dönüştürmek için tasarlanmıştır."
        )
        ctk.CTkLabel(
            card,
            text=desc,
            font=ctk.CTkFont(size=12),
            text_color=THEME["text_secondary"],
            justify="center"
        ).pack(pady=(0, 20))

        # Specs grid
        features_frame = ctk.CTkFrame(card, fg_color=THEME["raised"], corner_radius=6)
        features_frame.pack(fill="x", padx=40, pady=(0, 20))

        f_text = (
            "• Toplu PDF dönüştürme ve recursive klasör tarama\n"
            "• Özel veya varsayılan kurum logosu desteği\n"
            "• Kalıcı kullanıcı profili ve akıllı ders adı algılama\n"
            "• Çevrimdışı ve gizlilik odaklı mimari (hiçbir veri dışarı aktarılmaz)\n"
            "• Vektörel netlikte 144 DPI baskı optimizasyonu"
        )
        ctk.CTkLabel(
            features_frame,
            text=f_text,
            font=ctk.CTkFont(size=12),
            text_color=THEME["text_primary"],
            justify="left"
        ).pack(padx=20, pady=16)

        author_box = ctk.CTkFrame(card, fg_color="transparent")
        author_box.pack(pady=(0, 20))

        ctk.CTkLabel(
            author_box,
            text="Geliştirici: Haluk Tanrıseven (@haluked)  ·  MIT Lisansı",
            font=ctk.CTkFont(size=11),
            text_color=THEME["text_muted"]
        ).pack()

        ctk.CTkButton(
            card,
            text="GitHub Sayfasını Ziyaret Et",
            font=ctk.CTkFont(size=12),
            command=lambda: webbrowser.open("https://github.com/haluked/slide-note-formatter"),
            width=220,
            height=34,
            fg_color=THEME["raised"],
            hover_color=THEME["raised_hover"],
            text_color=THEME["text_primary"],
            border_width=1,
            border_color=THEME["border"],
            corner_radius=6
        ).pack(pady=(0, 20))

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
        self.update_preview_canvas()

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

    def remove_file(self, file_path):
        if file_path in self.selected_files:
            self.selected_files.remove(file_path)
            self.update_file_list()

    def auto_detect_course(self):
        if self.selected_files:
            detected = detect_course_name(self.selected_files)
            if detected:
                self.ent_course.delete(0, "end")
                self.ent_course.insert(0, detected)
                self.update_preview_canvas()

    def clear_files(self):
        self.selected_files = []
        self.ent_course.delete(0, "end")
        self.update_file_list()
        self.update_preview_canvas()

    def update_file_list(self):
        # Clear existing children in scrollable frame
        for child in self.file_list_frame.winfo_children():
            child.destroy()

        count = len(self.selected_files)
        self.lbl_file_count.configure(text=f"{count} dosya")

        if count == 0:
            empty_frame = ctk.CTkFrame(self.file_list_frame, fg_color="transparent")
            empty_frame.pack(fill="both", expand=True, pady=40)

            ctk.CTkLabel(
                empty_frame,
                text="İşlenecek PDF dosyası bulunmuyor",
                font=ctk.CTkFont(size=13, weight="bold"),
                text_color=THEME["text_secondary"]
            ).pack(pady=(0, 4))

            ctk.CTkLabel(
                empty_frame,
                text="Dosya veya klasör seçerek kuyruğa ekleyin",
                font=ctk.CTkFont(size=12),
                text_color=THEME["text_muted"]
            ).pack()
        else:
            for idx, fpath in enumerate(self.selected_files, 1):
                row = ctk.CTkFrame(
                    self.file_list_frame,
                    fg_color=THEME["card"],
                    corner_radius=6,
                    border_width=1,
                    border_color=THEME["card_border"]
                )
                row.pack(fill="x", pady=2, padx=2)

                # Index pill
                ctk.CTkLabel(
                    row,
                    text=f"{idx:02d}",
                    font=ctk.CTkFont(family="Consolas", size=11),
                    text_color=THEME["text_muted"],
                    width=28
                ).pack(side="left", padx=(8, 4))

                # Text info
                text_frame = ctk.CTkFrame(row, fg_color="transparent")
                text_frame.pack(side="left", fill="both", expand=True, pady=6)

                fname = os.path.basename(fpath)
                fdir = os.path.dirname(fpath)

                ctk.CTkLabel(
                    text_frame,
                    text=fname,
                    font=ctk.CTkFont(size=12, weight="bold"),
                    text_color=THEME["text_primary"],
                    anchor="w"
                ).pack(fill="x")

                ctk.CTkLabel(
                    text_frame,
                    text=fdir,
                    font=ctk.CTkFont(size=10),
                    text_color=THEME["text_muted"],
                    anchor="w"
                ).pack(fill="x")

                # Remove button (×)
                btn_remove = ctk.CTkButton(
                    row,
                    text="×",
                    width=28,
                    height=28,
                    fg_color="transparent",
                    hover_color=THEME["raised_hover"],
                    text_color=THEME["text_muted"],
                    font=ctk.CTkFont(size=14),
                    command=lambda p=fpath: self.remove_file(p)
                )
                btn_remove.pack(side="right", padx=6)

    def start_conversion(self):
        if not self.selected_files:
            messagebox.showwarning("Uyarı", "Lütfen önce en az bir PDF dosyası seçin.")
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
            self.update_status(f"{success_count}/{total_files} dosya tamamlandı, bazı hatalar oluştu.", 1.0)
        else:
            self.update_status(f"Tamamlandı: {success_count}/{total_files} dosya oluşturuldu.", 1.0)

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
                f"{success_count}/{total_files} PDF başarıyla dönüştürüldü.\n\n"
                f"Kayıt Konumu:\n{self.last_output_dir}"
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
