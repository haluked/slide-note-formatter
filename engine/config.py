import os
import json

CONFIG_DIR = os.path.expanduser("~/.slide_note_formatter")
CONFIG_FILE = os.path.join(CONFIG_DIR, "config.json")

DEFAULT_CONFIG = {
    "user_name": "",
    "university_name": "",
    "subtitle": "Lecture Notes & Study Handouts",
    "custom_logo_path": "",
    "default_preset": "3 Slayt (Kapaklı)",
    "slides_per_page": 3,
    "dpi": 144,
    "jpg_quality": 75,
    "grayscale": True,
    "draw_border": False,
    "crop_mm": 1.0,
    "watermark_opacity": 0.5,
    "watermark_fontsize": 7,
    "dest_mode": "subfolder",
    "custom_dest_folder": "",
    "appearance_mode": "dark"
}


class ConfigManager:
    def __init__(self):
        self.config = dict(DEFAULT_CONFIG)
        self.load()

    def load(self):
        try:
            if os.path.exists(CONFIG_FILE):
                with open(CONFIG_FILE, "r", encoding="utf-8") as f:
                    data = json.load(f)
                    self.config.update(data)
        except Exception as e:
            print("Config load error:", e)

    def save(self):
        try:
            os.makedirs(CONFIG_DIR, exist_ok=True)
            with open(CONFIG_FILE, "w", encoding="utf-8") as f:
                json.dump(self.config, f, ensure_ascii=False, indent=2)
        except Exception as e:
            print("Config save error:", e)

    def get(self, key, default=None):
        return self.config.get(key, default if default is not None else DEFAULT_CONFIG.get(key))

    def set(self, key, value):
        self.config[key] = value

    def reset_to_defaults(self):
        self.config = dict(DEFAULT_CONFIG)
        self.save()
