import React, { useState, useEffect, useRef } from 'react';
import {
  FolderOpen,
  Settings,
  Sun,
  Moon,
  Play,
  X,
  Plus
} from 'lucide-react';

import { invoke } from '@tauri-apps/api/core';
import { listen } from '@tauri-apps/api/event';

const tauriInvoke = invoke;
const tauriListen = listen;

interface FileItem {
  id: string;
  name: string;
  path: string;
  dir: string;
  size: number;
}

interface AppConfig {
  user_name: string;
  university_name: string;
  subtitle: string;
  custom_logo_path: string;
  slides_per_page: number;
  add_cover: boolean;
  add_end: boolean;
  grayscale: boolean;
  draw_border: boolean;
  dpi: number;
  default_preset: string;
  dest_mode: 'subfolder' | 'custom';
  custom_dest_folder: string;
  appearance_mode: 'dark' | 'light' | 'system';
}

const DEFAULT_CONFIG: AppConfig = {
  user_name: "",
  university_name: "",
  subtitle: "Lecture Notes & Study Handouts",
  custom_logo_path: "",
  slides_per_page: 3,
  add_cover: true,
  add_end: true,
  grayscale: true,
  draw_border: false,
  dpi: 144,
  default_preset: "3 Slayt (Kapaklı)",
  dest_mode: "subfolder",
  custom_dest_folder: "",
  appearance_mode: "dark",
};

const PRESETS = [
  "3 Slayt (Kapaklı)",
  "2 Slayt (Kapaklı)",
  "3 Slayt (Kapaksız)",
  "2 Slayt (Kapaksız)",
  "4 Slayt (Kompakt)",
];

export default function App() {
  const [config, setConfig] = useState<AppConfig>(DEFAULT_CONFIG);
  const [files, setFiles] = useState<FileItem[]>([]);
  const [courseName, setCourseName] = useState("");
  const [previewMode, setPreviewMode] = useState<'slides' | 'cover'>('slides');
  const [isProcessing, setIsProcessing] = useState(false);
  const [progress, setProgress] = useState({ percent: 0, status: "Hazır", detail: "" });
  const [lastOutputDir, setLastOutputDir] = useState<string | null>(null);
  const [isSettingsOpen, setIsSettingsOpen] = useState(false);
  const [settingsTab, setSettingsTab] = useState<'profile' | 'print'>('profile');
  const [saveToast, setSaveToast] = useState<string | null>(null);
  const [theme, setTheme] = useState<'dark' | 'light' | 'system'>('dark');
  const fileInputRef = useRef<HTMLInputElement>(null);

  // Load config on mount
  useEffect(() => {
    async function loadConfig() {
      if (tauriInvoke) {
        try {
          const cfg = await tauriInvoke<any>('get_user_config');
          if (cfg) {
            setConfig((prev) => ({ ...prev, ...cfg }));
            setTheme(cfg.appearance_mode || 'dark');
            return;
          }
        } catch (e) {
          console.error("Failed to load config from Tauri", e);
        }
      }
      const local = localStorage.getItem('slide_note_config');
      if (local) {
        try {
          const parsed = JSON.parse(local);
          setConfig(parsed);
          setTheme(parsed.appearance_mode || 'dark');
        } catch (e) {}
      }
    }
    loadConfig();
  }, []);

  // Listen to batch progress events from Tauri
  useEffect(() => {
    if (tauriListen) {
      const unlisten = tauriListen('batch-progress', (event: any) => {
        const payload = event.payload;
        if (payload.type === 'progress') {
          const pct = Math.round(payload.progress * 100);
          setProgress({
            percent: pct,
            status: `İşleniyor (${payload.file_idx + 1}/${payload.total_files}): ${payload.file_name}`,
            detail: `Slayt [${payload.slide}/${payload.total_slides}]`
          });
        } else if (payload.type === 'done') {
          setIsProcessing(false);
          setProgress({
            percent: 100,
            status: `✓ Tamamlandı: ${payload.success_count}/${payload.total_files} PDF oluşturuldu.`,
            detail: ""
          });
          if (payload.output_dir) {
            setLastOutputDir(payload.output_dir);
          }
        }
      });
      return () => {
        unlisten.then((fn: any) => fn());
      };
    }
  }, []);

  // Apply Theme
  useEffect(() => {
    const root = document.documentElement;
    const isDark =
      theme === 'dark' ||
      (theme === 'system' && window.matchMedia('(prefers-color-scheme: dark)').matches);

    if (isDark) {
      root.classList.add('dark');
    } else {
      root.classList.remove('dark');
    }
  }, [theme]);

  // Global Keyboard Shortcuts
  useEffect(() => {
    function handleKeyDown(e: KeyboardEvent) {
      if ((e.ctrlKey || e.metaKey) && (e.key === 'o' || e.key === 'O')) {
        e.preventDefault();
        if (e.shiftKey) {
          handlePickFolder();
        } else {
          handlePickFiles();
        }
      } else if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
        e.preventDefault();
        if (!isProcessing && files.length > 0) {
          handleStartConversion();
        }
      } else if (e.key === 'Escape') {
        if (isSettingsOpen) {
          setIsSettingsOpen(false);
        }
      }
    }
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [files, isProcessing, isSettingsOpen, config, courseName]);

  // Format File Size
  function formatSize(bytes: number): string {
    if (bytes >= 1024 * 1024) {
      return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
    }
    return `${Math.max(1, Math.round(bytes / 1024))} KB`;
  }

  // Course Detection
  function autoDetectCourse(paths: string[]) {
    if (paths.length === 0) return;
    for (const p of paths) {
      const match = p.match(/\b([A-Za-z]{2,5}\s*\d{2,4}[A-Za-z]?)\b/);
      if (match) {
        setCourseName(match[1].toUpperCase());
        return;
      }
    }
    const sample = paths[0];
    const parts = sample.replace(/\\/g, '/').split('/');
    if (parts.length > 1) {
      const parent = parts[parts.length - 2];
      const generic = ["desktop", "downloads", "documents", "pdf", "pdfs", "slayt", "notes"];
      if (parent && !generic.includes(parent.toLowerCase()) && parent.length > 2) {
        setCourseName(parent.toUpperCase());
      }
    }
  }

  // Pick Files
  async function handlePickFiles() {
    if (tauriInvoke) {
      try {
        const selected = await tauriInvoke('pick_files');
        if (selected && Array.isArray(selected) && selected.length > 0) {
          addFilePaths(selected);
          return;
        }
      } catch (e) {
        console.warn("Tauri pick_files failed, falling back to input", e);
      }
    }
    fileInputRef.current?.click();
  }

  function handleHtmlFileInput(e: React.ChangeEvent<HTMLInputElement>) {
    const flist = e.target.files;
    if (!flist) return;
    const newItems: FileItem[] = [];
    for (let i = 0; i < flist.length; i++) {
      const f = flist[i];
      newItems.push({
        id: `${f.name}-${Date.now()}-${i}`,
        name: f.name,
        path: (f as any).path || f.name,
        dir: (f as any).path ? (f as any).path.substring(0, (f as any).path.lastIndexOf('\\')) : "Seçilen Dosya",
        size: f.size
      });
    }
    setFiles((prev) => {
      const merged = [...prev];
      for (const item of newItems) {
        if (!merged.some((m) => m.name === item.name)) {
          merged.push(item);
        }
      }
      autoDetectCourse(merged.map((m) => m.path));
      return merged;
    });
  }

  async function handlePickFolder() {
    if (tauriInvoke) {
      try {
        const folder = await tauriInvoke('pick_folder');
        if (folder) {
          const folderFiles = await tauriInvoke('scan_folder_for_pdfs', { folder });
          if (folderFiles && Array.isArray(folderFiles)) {
            addFilePaths(folderFiles);
          }
        }
      } catch (e) {
        console.error("Tauri pick_folder failed", e);
      }
    }
  }

  function addFilePaths(paths: string[]) {
    const newItems: FileItem[] = paths.map((p, idx) => {
      const name = p.replace(/\\/g, '/').split('/').pop() || p;
      const dir = p.replace(/\\/g, '/').substring(0, p.lastIndexOf('/')) || "";
      return {
        id: `${name}-${Date.now()}-${idx}`,
        name,
        path: p,
        dir,
        size: 1024 * 1024 * 1.5, // placeholder until stat
      };
    });
    setFiles((prev) => {
      const merged = [...prev];
      for (const item of newItems) {
        if (!merged.some((m) => m.path === item.path)) {
          merged.push(item);
        }
      }
      autoDetectCourse(merged.map((m) => m.path));
      return merged;
    });
  }

  function handleRemoveFile(id: string) {
    setFiles((prev) => prev.filter((f) => f.id !== id));
  }

  function handleClearFiles() {
    setFiles([]);
    setCourseName("");
    setLastOutputDir(null);
    setProgress({ percent: 0, status: "Hazır", detail: "" });
  }

  // Preset Selection
  function handlePresetChange(preset: string) {
    let slides = 3;
    let cover = true;
    let end = true;
    if (preset.includes("2 Slayt")) slides = 2;
    if (preset.includes("4 Slayt")) slides = 4;
    if (preset.includes("Kapaksız")) {
      cover = false;
      end = false;
    }
    setConfig((prev) => ({
      ...prev,
      default_preset: preset,
      slides_per_page: slides,
      add_cover: cover,
      add_end: end
    }));
  }

  // Save Settings
  async function handleSaveSettings() {
    if (tauriInvoke) {
      try {
        await tauriInvoke('save_user_config', { config });
      } catch (e) {
        console.error("Failed to save config to Tauri", e);
      }
    }
    localStorage.setItem('slide_note_config', JSON.stringify(config));
    setSaveToast("✓ Değişiklikler kaydedildi");
    setTimeout(() => setSaveToast(null), 2500);
  }

  // Start Conversion
  async function handleStartConversion() {
    if (files.length === 0 || isProcessing) return;
    setIsProcessing(true);
    setProgress({ percent: 0, status: "Dönüştürme başlatılıyor...", detail: "" });

    if (tauriInvoke) {
      try {
        const filePaths = files.map((f) => f.path);
        await tauriInvoke('process_batch', {
          files: filePaths,
          courseName,
          config
        });
        return;
      } catch (e: any) {
        setIsProcessing(false);
        setProgress({ percent: 0, status: `Hata: ${e}`, detail: "" });
        return;
      }
    }

    // Web simulation if not in Tauri window
    let p = 0;
    const interval = setInterval(() => {
      p += 0.2;
      if (p >= 1) {
        clearInterval(interval);
        setIsProcessing(false);
        setProgress({ percent: 100, status: `✓ Tamamlandı: ${files.length}/${files.length} PDF oluşturuldu.`, detail: "" });
        setLastOutputDir("C:\\Desktop\\_Not_Formatli");
      } else {
        setProgress({
          percent: Math.round(p * 100),
          status: `İşleniyor: ${files[0].name}`,
          detail: `Slayt [${Math.round(p * 12)}/12]`
        });
      }
    }, 400);
  }

  // Open output folder
  async function handleOpenOutputFolder() {
    if (!lastOutputDir) return;
    if (tauriInvoke) {
      try {
        await tauriInvoke('open_folder', { path: lastOutputDir });
        return;
      } catch (e) {
        console.error("Failed to open folder", e);
      }
    }
  }

  const profileTag = config.user_name && config.university_name
    ? `${config.user_name} · ${config.university_name}`
    : config.user_name || config.university_name || "Öğrenci Profili (Ayarlanmadı)";

  return (
    <div className="flex flex-col h-screen bg-[#F8F9FA] dark:bg-[#121316] text-[#16181D] dark:text-[#EDEDF0] transition-colors select-none">
      {/* Hidden file input for web fallback */}
      <input
        type="file"
        ref={fileInputRef}
        onChange={handleHtmlFileInput}
        multiple
        accept=".pdf"
        className="hidden"
      />

      {/* TOP HEADER BAR */}
      <header className="flex items-center justify-between px-6 py-3 border-b border-[#E2E5E9] dark:border-[#282A33] bg-white dark:bg-[#18191E] shrink-0">
        <div className="flex items-center gap-3">
          <div className="flex items-center justify-center w-8 h-8 rounded-lg bg-blue-600/10 text-blue-600 dark:text-blue-400 font-bold text-sm">
            PDF
          </div>
          <div className="flex items-center gap-2">
            <h1 className="text-base font-semibold tracking-tight">Slayt Not Formatlayıcı</h1>
            <span className="px-2 py-0.5 text-[10px] font-mono rounded bg-[#F1F3F5] dark:bg-[#21232B] text-[#858B98] dark:text-[#656B77] border border-[#E2E5E9] dark:border-[#282A33]">
              v2.0 · Tauri (Rust)
            </span>
          </div>
        </div>

        <div className="flex items-center gap-4">
          <div className="text-xs text-[#858B98] dark:text-[#656B77] hidden sm:block truncate max-w-xs">
            {profileTag}
          </div>

          {/* Theme switcher */}
          <button
            onClick={() => {
              const next = theme === 'dark' ? 'light' : 'dark';
              setTheme(next);
              setConfig((prev) => ({ ...prev, appearance_mode: next }));
            }}
            className="p-1.5 rounded-md hover:bg-[#F1F3F5] dark:hover:bg-[#21232B] text-[#505565] dark:text-[#9DA3AF] transition-colors"
            title="Temayı Değiştir"
          >
            {theme === 'dark' ? <Sun className="w-4 h-4" /> : <Moon className="w-4 h-4" />}
          </button>

          {/* Settings Trigger */}
          <button
            onClick={() => setIsSettingsOpen(true)}
            className="flex items-center gap-1.5 px-3 py-1.5 text-xs font-medium rounded-md border border-[#D8DCE3] dark:border-[#2C2E38] bg-[#F1F3F5] dark:bg-[#21232B] hover:bg-[#E9ECEF] dark:hover:bg-[#2A2D37] transition-all"
          >
            <Settings className="w-3.5 h-3.5 text-[#505565] dark:text-[#9DA3AF]" />
            <span>Ayarlar</span>
          </button>
        </div>
      </header>

      {/* MAIN SPLIT VIEW */}
      <main className="flex-1 flex overflow-hidden p-4 gap-4">
        {/* LEFT COLUMN: File Queue */}
        <div className="w-1/2 flex flex-col bg-white dark:bg-[#18191E] rounded-xl border border-[#E2E5E9] dark:border-[#282A33] overflow-hidden shadow-xs">
          <div className="flex items-center justify-between px-4 py-3 border-b border-[#E2E5E9] dark:border-[#282A33]">
            <div className="flex items-center gap-2">
              <span className="text-sm font-semibold">Slayt Dosyaları</span>
              <span className="px-2 py-0.5 text-xs font-mono rounded-full bg-[#F1F3F5] dark:bg-[#21232B] text-[#505565] dark:text-[#9DA3AF]">
                {files.length} dosya
              </span>
            </div>
            {files.length > 0 && (
              <button
                onClick={handleClearFiles}
                className="text-xs text-[#858B98] hover:text-red-500 transition-colors"
              >
                Temizle (Esc)
              </button>
            )}
          </div>

          {/* Action Row */}
          <div className="flex items-center gap-2 p-3 bg-[#F8F9FA] dark:bg-[#15161A] border-b border-[#E2E5E9] dark:border-[#282A33]">
            <button
              onClick={handlePickFiles}
              className="flex items-center gap-1.5 px-3 py-1.5 text-xs font-medium rounded-lg bg-white dark:bg-[#21232B] border border-[#D8DCE3] dark:border-[#2C2E38] hover:bg-[#F1F3F5] dark:hover:bg-[#2A2D37] transition-all active:scale-[0.98]"
            >
              <Plus className="w-3.5 h-3.5 text-blue-600 dark:text-blue-400" />
              <span>+ Dosya Ekle (Ctrl+O)</span>
            </button>
            <button
              onClick={handlePickFolder}
              className="flex items-center gap-1.5 px-3 py-1.5 text-xs font-medium rounded-lg bg-white dark:bg-[#21232B] border border-[#D8DCE3] dark:border-[#2C2E38] hover:bg-[#F1F3F5] dark:hover:bg-[#2A2D37] transition-all active:scale-[0.98]"
            >
              <FolderOpen className="w-3.5 h-3.5 text-[#505565] dark:text-[#9DA3AF]" />
              <span>Klasör Tara (Ctrl+Shift+O)</span>
            </button>
          </div>

          {/* Scrollable File List / Empty State */}
          <div className="flex-1 overflow-y-auto p-3">
            {files.length === 0 ? (
              <div className="h-full flex flex-col items-center justify-center p-6 text-center border-2 border-dashed border-[#E2E5E9] dark:border-[#282A33] rounded-lg">
                <div className="flex items-center justify-center w-12 h-14 rounded-lg bg-[#F1F3F5] dark:bg-[#21232B] border border-[#D8DCE3] dark:border-[#2C2E38] mb-3">
                  <span className="font-mono text-xs font-bold text-blue-600 dark:text-blue-400">PDF</span>
                </div>
                <h3 className="text-sm font-semibold mb-1">İşlenecek PDF Dosyası Yok</h3>
                <p className="text-xs text-[#858B98] dark:text-[#656B77] mb-4 max-w-xs">
                  Ders slayt sunumlarınızı sürükleyip buraya bırakın veya butonları kullanarak seçin.
                </p>
                <div className="flex items-center gap-2">
                  <button
                    onClick={handlePickFiles}
                    className="px-3.5 py-1.5 text-xs font-semibold rounded-md bg-[#16181D] dark:bg-[#F1F5F9] text-white dark:text-[#0F172A] hover:bg-[#2B2E36] dark:hover:bg-[#E2E8F0] transition-all"
                  >
                    Dosya Seç
                  </button>
                  <button
                    onClick={handlePickFolder}
                    className="px-3.5 py-1.5 text-xs font-medium rounded-md border border-[#D8DCE3] dark:border-[#2C2E38] hover:bg-[#F1F3F5] dark:hover:bg-[#21232B] transition-all"
                  >
                    Klasör Tara
                  </button>
                </div>
              </div>
            ) : (
              <div className="space-y-1.5">
                {files.map((file, idx) => (
                  <div
                    key={file.id}
                    className="flex items-center justify-between p-2.5 rounded-lg border border-[#E2E5E9] dark:border-[#282A33] bg-[#F8F9FA] dark:bg-[#1E2027] hover:border-blue-500/40 transition-all group"
                  >
                    <div className="flex items-center gap-2.5 min-w-0">
                      <span className="font-mono text-xs text-[#858B98] dark:text-[#656B77] w-6 shrink-0 text-center">
                        {String(idx + 1).padStart(2, '0')}
                      </span>
                      <div className="min-w-0">
                        <p className="text-xs font-medium truncate">{file.name}</p>
                        <p className="text-[10px] text-[#858B98] dark:text-[#656B77] truncate">{file.dir}</p>
                      </div>
                    </div>
                    <div className="flex items-center gap-2 shrink-0 ml-2">
                      <span className="px-2 py-0.5 text-[10px] font-mono rounded bg-white dark:bg-[#282A33] border border-[#E2E5E9] dark:border-[#383B46] text-[#505565] dark:text-[#9DA3AF]">
                        {formatSize(file.size)}
                      </span>
                      <button
                        onClick={() => handleRemoveFile(file.id)}
                        className="p-1 rounded-md text-[#858B98] hover:text-red-500 hover:bg-red-50 dark:hover:bg-red-950/30 transition-colors"
                        title="Dosyayı Kaldır"
                      >
                        <X className="w-3.5 h-3.5" />
                      </button>
                    </div>
                  </div>
                ))}
              </div>
            )}
          </div>
        </div>

        {/* RIGHT COLUMN: Settings, Live Preview & Specs */}
        <div className="w-1/2 flex flex-col gap-4 overflow-y-auto pr-1">
          {/* Card 1: Preset & Course */}
          <div className="p-4 bg-white dark:bg-[#18191E] rounded-xl border border-[#E2E5E9] dark:border-[#282A33] shadow-xs">
            <h2 className="text-xs font-bold uppercase tracking-wider text-[#858B98] dark:text-[#656B77] mb-3">
              Şablon & Ders Bilgisi
            </h2>
            <div className="space-y-3">
              <div>
                <label className="block text-xs font-medium text-[#505565] dark:text-[#9DA3AF] mb-1">
                  Sayfa Düzeni Şablonu
                </label>
                <select
                  value={config.default_preset}
                  onChange={(e) => handlePresetChange(e.target.value)}
                  className="w-full text-xs font-medium py-2 px-3 rounded-lg border border-[#D8DCE3] dark:border-[#2C2E38] bg-[#F8F9FA] dark:bg-[#21232B] focus:outline-hidden focus:border-blue-600 transition-colors"
                >
                  {PRESETS.map((p) => (
                    <option key={p} value={p}>{p}</option>
                  ))}
                </select>
              </div>

              <div>
                <label className="block text-xs font-medium text-[#505565] dark:text-[#9DA3AF] mb-1">
                  Ders Adı (Kapak ve Önizleme)
                </label>
                <input
                  type="text"
                  value={courseName}
                  onChange={(e) => setCourseName(e.target.value)}
                  placeholder="Ders kodu / adı (örn: ELEC201, MAT101)"
                  className="w-full text-xs py-2 px-3 rounded-lg border border-[#D8DCE3] dark:border-[#2C2E38] bg-[#F8F9FA] dark:bg-[#21232B] focus:outline-hidden focus:border-blue-600 transition-colors"
                />
              </div>
            </div>
          </div>

          {/* Card 2: Live A4 Preview (Craft Component) */}
          <div className="p-4 bg-white dark:bg-[#18191E] rounded-xl border border-[#E2E5E9] dark:border-[#282A33] shadow-xs">
            <div className="flex items-center justify-between mb-3">
              <h2 className="text-xs font-bold uppercase tracking-wider text-[#858B98] dark:text-[#656B77]">
                Canlı A4 Sayfa Önizlemesi
              </h2>
              {/* Segmented Switcher */}
              <div className="flex p-0.5 rounded-lg bg-[#F1F3F5] dark:bg-[#21232B] border border-[#E2E5E9] dark:border-[#282A33]">
                <button
                  onClick={() => setPreviewMode('slides')}
                  className={`px-3 py-1 text-xs font-medium rounded-md transition-all ${
                    previewMode === 'slides'
                      ? 'bg-white dark:bg-[#2B2E36] text-blue-600 dark:text-blue-400 shadow-xs'
                      : 'text-[#858B98] hover:text-[#16181D] dark:hover:text-[#EDEDF0]'
                  }`}
                >
                  Not Sayfası
                </button>
                <button
                  onClick={() => setPreviewMode('cover')}
                  className={`px-3 py-1 text-xs font-medium rounded-md transition-all ${
                    previewMode === 'cover'
                      ? 'bg-white dark:bg-[#2B2E36] text-blue-600 dark:text-blue-400 shadow-xs'
                      : 'text-[#858B98] hover:text-[#16181D] dark:hover:text-[#EDEDF0]'
                  }`}
                >
                  Ön Kapak
                </button>
              </div>
            </div>

            <div className="flex gap-4 items-center">
              {/* A4 Sheet Canvas (ratio 1 : 1.414) */}
              <div className="w-36 h-52 bg-white rounded-md border border-[#CBD5E1] shadow-md relative overflow-hidden flex flex-col shrink-0 select-none">
                {previewMode === 'slides' ? (
                  // SLIDE HANDOUT PAGE VIEW
                  <div className="h-full flex flex-col justify-between p-2">
                    {/* Header Watermark */}
                    <div className="text-right text-[5px] font-mono text-[#94A3B8] truncate">
                      {config.user_name || "Filigran"}
                    </div>

                    {/* Slide Slots with Ruled Lines */}
                    <div className="flex-1 flex flex-col justify-around py-1">
                      {Array.from({ length: config.slides_per_page }).map((_, i) => (
                        <div key={i} className="flex gap-1.5 items-center">
                          {/* Mini Slide Box */}
                          <div className="w-14 h-9 rounded-xs bg-[#F1F5F9] border border-[#94A3B8] p-1 flex flex-col justify-between shrink-0">
                            <div className="h-1 bg-[#CBD5E1] rounded-xs w-3/4"></div>
                            <div className="space-y-0.5">
                              <div className="h-0.5 bg-[#E2E8F0] rounded-xs w-full"></div>
                              <div className="h-0.5 bg-[#E2E8F0] rounded-xs w-4/5"></div>
                            </div>
                            <span className="text-[4px] font-mono text-[#94A3B8] text-right">#{i + 1}</span>
                          </div>
                          {/* Ruled lines */}
                          <div className="flex-1 space-y-1">
                            <div className="h-px bg-[#E2E8F0] w-full"></div>
                            <div className="h-px bg-[#E2E8F0] w-full"></div>
                            <div className="h-px bg-[#E2E8F0] w-full"></div>
                          </div>
                        </div>
                      ))}
                    </div>

                    {/* Footer */}
                    <div className="text-center text-[5px] text-[#94A3B8]">
                      — Sayfa 1 / 12 —
                    </div>
                  </div>
                ) : (
                  // FRONT COVER PAGE VIEW
                  <div className="h-full flex flex-col items-center justify-between p-3 text-center">
                    {!config.add_cover ? (
                      <div className="h-full flex flex-col items-center justify-center text-center">
                        <span className="text-[8px] font-semibold text-[#64748B]">Kapak Devre Dışı</span>
                        <span className="text-[6px] text-[#94A3B8]">(Kapaksız Şablon)</span>
                      </div>
                    ) : (
                      <>
                        <div className="text-[6px] font-bold text-[#475569] uppercase tracking-wider mt-1 truncate max-w-full">
                          {config.university_name || "ÜNİVERSİTE / KURUM"}
                        </div>

                        {/* Vector Crest Emblem */}
                        <div className="w-7 h-8 relative flex items-center justify-center my-1">
                          <svg viewBox="0 0 24 28" className="w-full h-full text-blue-600 fill-white stroke-current">
                            <polygon points="2,2 22,2 22,18 12,26 2,18" strokeWidth="1.5" />
                            <line x1="6" y1="8" x2="18" y2="8" stroke="#94A3B8" strokeWidth="1" />
                            <polyline points="8,14 12,18 16,14" stroke="#2563EB" strokeWidth="1.5" fill="none" />
                          </svg>
                        </div>

                        <div className="my-auto">
                          <div className="text-[8px] font-bold text-[#0F172A] leading-tight truncate max-w-[120px]">
                            {courseName || "DERS ADI"}
                          </div>
                          <div className="text-[5px] text-[#64748B] truncate max-w-[120px] mt-0.5">
                            {config.subtitle}
                          </div>
                          <div className="w-8 h-px bg-[#CBD5E1] mx-auto my-1"></div>
                        </div>

                        <div className="px-2 py-0.5 rounded bg-[#F8FAFC] border border-[#E2E8F0] text-[5px] text-[#475569] truncate max-w-full mb-1">
                          {config.user_name || "Öğrenci Notu"}
                        </div>
                      </>
                    )}
                  </div>
                )}
              </div>

              {/* Specs Details beside Preview */}
              <div className="flex-1 space-y-1.5 text-xs text-[#505565] dark:text-[#9DA3AF]">
                <div className="flex items-center gap-1.5">
                  <span className="font-medium text-[#16181D] dark:text-[#EDEDF0]">Format:</span>
                  <span>A4 Dikey (210×297 mm)</span>
                </div>
                <div className="flex items-center gap-1.5">
                  <span className="font-medium text-[#16181D] dark:text-[#EDEDF0]">Düzen:</span>
                  <span>{config.slides_per_page} Slayt + Sağ Not Alanı</span>
                </div>
                <div className="flex items-center gap-1.5">
                  <span className="font-medium text-[#16181D] dark:text-[#EDEDF0]">Kapak:</span>
                  <span>{config.add_cover ? "Var (Kurum Arması + Başlık)" : "Kapaksız"}</span>
                </div>
                <div className="flex items-center gap-1.5">
                  <span className="font-medium text-[#16181D] dark:text-[#EDEDF0]">Baskı:</span>
                  <span>{config.grayscale ? "Gri Tonlama" : "Renkli"} / {config.dpi} DPI</span>
                </div>
                <div className="flex items-center gap-1.5">
                  <span className="font-medium text-[#16181D] dark:text-[#EDEDF0]">Filigran:</span>
                  <span className="truncate">{config.user_name || "Belirtilmedi"}</span>
                </div>
              </div>
            </div>
          </div>
        </div>
      </main>

      {/* BOTTOM ACTION DOCK */}
      <footer className="p-4 border-t border-[#E2E5E9] dark:border-[#282A33] bg-white dark:bg-[#18191E] shrink-0">
        {/* Progress bar */}
        <div className="w-full bg-[#F1F3F5] dark:bg-[#21232B] h-1.5 rounded-full overflow-hidden mb-3">
          <div
            className="bg-blue-600 h-full transition-all duration-300"
            style={{ width: `${progress.percent}%` }}
          ></div>
        </div>

        <div className="flex items-center justify-between">
          <div className="flex items-center gap-3">
            <span className="text-xs text-[#505565] dark:text-[#9DA3AF]">{progress.status}</span>
            {progress.percent > 0 && progress.percent < 100 && (
              <span className="text-xs font-mono font-bold text-blue-600 dark:text-blue-400">
                {progress.percent}%
              </span>
            )}
          </div>

          <div className="flex items-center gap-3">
            {lastOutputDir && (
              <button
                onClick={handleOpenOutputFolder}
                className="flex items-center gap-1.5 px-4 py-2 text-xs font-semibold rounded-lg bg-blue-600 text-white hover:bg-blue-700 transition-all shadow-xs"
              >
                <FolderOpen className="w-3.5 h-3.5" />
                <span>Çıktı Klasörünü Aç</span>
              </button>
            )}

            <button
              onClick={handleStartConversion}
              disabled={files.length === 0 || isProcessing}
              className={`flex items-center gap-2 px-5 py-2 text-xs font-bold rounded-lg transition-all ${
                files.length > 0 && !isProcessing
                  ? 'bg-[#16181D] dark:bg-[#F1F5F9] text-white dark:text-[#0F172A] hover:bg-[#2B2E36] dark:hover:bg-[#E2E8F0] active:scale-[0.98] shadow-xs cursor-pointer'
                  : 'bg-[#F1F3F5] dark:bg-[#21232B] text-[#858B98] cursor-not-allowed'
              }`}
            >
              <Play className="w-3.5 h-3.5 fill-current" />
              <span>Dönüştürmeyi Başlat (Ctrl+↵)</span>
            </button>
          </div>
        </div>
      </footer>

      {/* SETTINGS SLIDE-OVER / MODAL */}
      {isSettingsOpen && (
        <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/40 backdrop-blur-xs p-4">
          <div className="w-full max-w-lg bg-white dark:bg-[#18191E] rounded-xl border border-[#E2E5E9] dark:border-[#282A33] shadow-2xl overflow-hidden flex flex-col">
            <div className="flex items-center justify-between px-5 py-4 border-b border-[#E2E5E9] dark:border-[#282A33]">
              <h2 className="text-sm font-bold">Ayarlar & Tercihler</h2>
              <button
                onClick={() => setIsSettingsOpen(false)}
                className="p-1 rounded-md text-[#858B98] hover:text-[#16181D] dark:hover:text-white"
              >
                <X className="w-4 h-4" />
              </button>
            </div>

            {/* Tab switch */}
            <div className="flex border-b border-[#E2E5E9] dark:border-[#282A33] px-5 bg-[#F8F9FA] dark:bg-[#15161A]">
              <button
                onClick={() => setSettingsTab('profile')}
                className={`py-2.5 px-3 text-xs font-semibold border-b-2 transition-all ${
                  settingsTab === 'profile'
                    ? 'border-blue-600 text-blue-600 dark:text-blue-400'
                    : 'border-transparent text-[#858B98]'
                }`}
              >
                Profil & Logo
              </button>
              <button
                onClick={() => setSettingsTab('print')}
                className={`py-2.5 px-3 text-xs font-semibold border-b-2 transition-all ${
                  settingsTab === 'print'
                    ? 'border-blue-600 text-blue-600 dark:text-blue-400'
                    : 'border-transparent text-[#858B98]'
                }`}
              >
                Baskı & Çıktı
              </button>
            </div>

            {/* Tab Content */}
            <div className="p-5 space-y-4 max-h-[60vh] overflow-y-auto">
              {settingsTab === 'profile' ? (
                <>
                  <div>
                    <label className="block text-xs font-medium mb-1">Öğrenci / Kullanıcı Adı (Filigran & Kapak)</label>
                    <input
                      type="text"
                      value={config.user_name}
                      onChange={(e) => setConfig({ ...config, user_name: e.target.value })}
                      placeholder="Örn: Haluk Tanrıseven"
                      className="w-full text-xs py-2 px-3 rounded-lg border border-[#D8DCE3] dark:border-[#2C2E38] bg-[#F8F9FA] dark:bg-[#21232B] focus:outline-hidden focus:border-blue-600"
                    />
                  </div>
                  <div>
                    <label className="block text-xs font-medium mb-1">Üniversite / Kurum Adı</label>
                    <input
                      type="text"
                      value={config.university_name}
                      onChange={(e) => setConfig({ ...config, university_name: e.target.value })}
                      placeholder="Örn: Politecnico di Torino"
                      className="w-full text-xs py-2 px-3 rounded-lg border border-[#D8DCE3] dark:border-[#2C2E38] bg-[#F8F9FA] dark:bg-[#21232B] focus:outline-hidden focus:border-blue-600"
                    />
                  </div>
                  <div>
                    <label className="block text-xs font-medium mb-1">Kapak Alt Başlığı</label>
                    <input
                      type="text"
                      value={config.subtitle}
                      onChange={(e) => setConfig({ ...config, subtitle: e.target.value })}
                      placeholder="Örn: Lecture Notes & Study Handouts"
                      className="w-full text-xs py-2 px-3 rounded-lg border border-[#D8DCE3] dark:border-[#2C2E38] bg-[#F8F9FA] dark:bg-[#21232B] focus:outline-hidden focus:border-blue-600"
                    />
                  </div>
                </>
              ) : (
                <>
                  <div>
                    <label className="block text-xs font-medium mb-1.5">Baskı Çözünürlüğü (DPI)</label>
                    <div className="grid grid-cols-3 gap-2">
                      {[100, 144, 200].map((dpiVal) => (
                        <button
                          key={dpiVal}
                          onClick={() => setConfig({ ...config, dpi: dpiVal })}
                          className={`py-2 px-3 text-xs font-medium rounded-lg border transition-all ${
                            config.dpi === dpiVal
                              ? 'border-blue-600 bg-blue-50 dark:bg-blue-950/40 text-blue-600 dark:text-blue-400'
                              : 'border-[#D8DCE3] dark:border-[#2C2E38] bg-[#F8F9FA] dark:bg-[#21232B]'
                          }`}
                        >
                          {dpiVal} DPI {dpiVal === 144 ? '(Önerilen)' : ''}
                        </button>
                      ))}
                    </div>
                  </div>

                  <div className="space-y-3 pt-2">
                    <label className="flex items-center gap-2 cursor-pointer">
                      <input
                        type="checkbox"
                        checked={config.grayscale}
                        onChange={(e) => setConfig({ ...config, grayscale: e.target.checked })}
                        className="rounded border-[#D8DCE3] text-blue-600"
                      />
                      <span className="text-xs">Gri tonlama (Toner tasarrufu için siyah-beyaz fotokopi)</span>
                    </label>

                    <label className="flex items-center gap-2 cursor-pointer">
                      <input
                        type="checkbox"
                        checked={config.draw_border}
                        onChange={(e) => setConfig({ ...config, draw_border: e.target.checked })}
                        className="rounded border-[#D8DCE3] text-blue-600"
                      />
                      <span className="text-xs">Slayt etrafına ince gri çerçeve ekle</span>
                    </label>

                    <label className="flex items-center gap-2 cursor-pointer">
                      <input
                        type="checkbox"
                        checked={config.add_cover}
                        onChange={(e) => setConfig({ ...config, add_cover: e.target.checked })}
                        className="rounded border-[#D8DCE3] text-blue-600"
                      />
                      <span className="text-xs">Ön kapak sayfası oluştur (Logo, ders ve modül başlığı)</span>
                    </label>

                    <label className="flex items-center gap-2 cursor-pointer">
                      <input
                        type="checkbox"
                        checked={config.add_end}
                        onChange={(e) => setConfig({ ...config, add_end: e.target.checked })}
                        className="rounded border-[#D8DCE3] text-blue-600"
                      />
                      <span className="text-xs">Bitiş sayfası ekle (- END OF MODULE -)</span>
                    </label>
                  </div>
                </>
              )}
            </div>

            {/* Footer */}
            <div className="flex items-center justify-between px-5 py-3.5 border-t border-[#E2E5E9] dark:border-[#282A33] bg-[#F8F9FA] dark:bg-[#15161A]">
              <div className="text-xs font-semibold text-emerald-600">
                {saveToast}
              </div>
              <div className="flex items-center gap-2">
                <button
                  onClick={() => setIsSettingsOpen(false)}
                  className="px-3.5 py-1.5 text-xs font-medium rounded-lg border border-[#D8DCE3] dark:border-[#2C2E38] hover:bg-[#F1F3F5] dark:hover:bg-[#21232B] transition-all"
                >
                  Kapat
                </button>
                <button
                  onClick={handleSaveSettings}
                  className="px-4 py-1.5 text-xs font-bold rounded-lg bg-[#16181D] dark:bg-[#F1F5F9] text-white dark:text-[#0F172A] hover:bg-[#2B2E36] dark:hover:bg-[#E2E8F0] transition-all"
                >
                  Kaydet
                </button>
              </div>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}

