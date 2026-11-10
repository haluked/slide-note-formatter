#define _CRT_SECURE_NO_WARNINGS
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <math.h>
#include "pdf_engine.h"
#include "gb_ui.h"

#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

#define ID_BTN_BROWSE       101
#define ID_BTN_CONVERT      102
#define ID_BTN_OPEN_FOLDER  103
#define ID_LST_FILES        104
#define ID_CMB_SLIDES       105
#define ID_CHK_BORDER       106
#define ID_CHK_COVER        107
#define ID_CHK_END          108
#define ID_EDT_COURSE       109
#define ID_EDT_MODULE       110
#define ID_EDT_AUTHOR       111
#define ID_EDT_UNI          112
#define ID_CHK_SUBFOLDER    113
#define ID_EDT_OUTDIR       114
#define ID_BTN_BROWSE_OUT   115
#define ID_BTN_CLEAR        116
#define ID_BTN_MIN          117
#define ID_BTN_CLOSE        118
#define ID_CHK_NOTE         119
#define ID_CMB_NOTE_SIDE    120
#define ID_EDT_NOTE_W       121
#define ID_EDT_GAP          122
#define ID_CHK_LINES        123

#define BASE_CLIENT_W       646
#define BASE_CLIENT_H       674

#define APP_WINDOW_STYLE    (WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU | WS_CLIPCHILDREN)
#define APP_WINDOW_EX_STYLE (WS_EX_APPWINDOW | WS_EX_CONTROLPARENT | WS_EX_ACCEPTFILES)

#define GB_MAX_TRACKED 24

typedef struct {
    HWND hwnd;
    WNDPROC old_proc;
    int hover;
} GbTrackedButton;

static HWND g_hwnd = NULL;
static HWND g_hListFiles = NULL;
static HWND g_hCmbSlides = NULL;
static HWND g_hChkBorder = NULL;
static HWND g_hChkCover = NULL;
static HWND g_hChkEnd = NULL;
static HWND g_hEdtCourse = NULL;
static HWND g_hEdtModule = NULL;
static HWND g_hEdtAuthor = NULL;
static HWND g_hEdtUni = NULL;
static HWND g_hChkSubfolder = NULL;
static HWND g_hEdtOutDir = NULL;
static HWND g_hChkNote = NULL;
static HWND g_hCmbNoteSide = NULL;
static HWND g_hEdtNoteW = NULL;
static HWND g_hEdtGap = NULL;
static HWND g_hChkLines = NULL;
static HWND g_hBtnBrowse = NULL;
static HWND g_hBtnClear = NULL;
static HWND g_hBtnBrowseOut = NULL;
static HWND g_hBtnConvert = NULL;
static HWND g_hBtnOpenFolder = NULL;
static HWND g_hStatus = NULL;
static HWND g_hBtnMin = NULL;
static HWND g_hBtnClose = NULL;
static HWND g_hLblOut = NULL;
static HWND g_hLblSlides = NULL;
static HWND g_hLblNoteSide = NULL;
static HWND g_hLblNoteW = NULL;
static HWND g_hLblNoteWUnit = NULL;
static HWND g_hLblGap = NULL;
static HWND g_hLblGapUnit = NULL;
static HWND g_hLblCourse = NULL;
static HWND g_hLblModule = NULL;
static HWND g_hLblAuthor = NULL;
static HWND g_hLblUni = NULL;

static HFONT g_fontTitle = NULL;
static HFONT g_fontBold = NULL;
static HFONT g_fontNormal = NULL;
static HFONT g_fontSmall = NULL;

static HBRUSH g_brushLight = NULL;
static HBRUSH g_brushDark = NULL;

static UINT g_dpi = 96;
static int g_progress = 0;

static int g_chk_subfolder = 1;
static int g_chk_border = 1;
static int g_chk_cover = 0;
static int g_chk_end = 0;
static int g_slides_sel = 0;
static int g_note_enabled = 1;
static int g_note_side = 0;
static double g_note_width_mm = 84.0;
static double g_slide_gap_mm = 5.0;
static int g_draw_note_lines = 0;

static const wchar_t *g_slide_items[3] = {
    L"3 SLIDES",
    L"2 SLIDES",
    L"4 SLIDES"
};

static const wchar_t *g_note_side_items[2] = {
    L"RIGHT",
    L"LEFT"
};

static GbTrackedButton g_tracked[GB_MAX_TRACKED];
static int g_tracked_count = 0;

static RECT g_rc_panel_files;
static RECT g_rc_panel_output;
static RECT g_rc_panel_layout;
static RECT g_rc_panel_cover;
static RECT g_rc_progress;

static wchar_t **g_input_files = NULL;
static int g_input_count = 0;
static int g_input_capacity = 0;
static wchar_t g_last_output_dir[MAX_PATH] = { 0 };

static int S(int value)
{
    return gb_scale(value);
}

/* ------------------------------------------------------------------ hover */

static GbTrackedButton *tracked_find(HWND hwnd)
{
    int i;
    for (i = 0; i < g_tracked_count; i++) {
        if (g_tracked[i].hwnd == hwnd) return &g_tracked[i];
    }
    return NULL;
}

static LRESULT CALLBACK TrackedButtonProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    GbTrackedButton *t = tracked_find(hwnd);
    WNDPROC old = t ? t->old_proc : NULL;

    if (msg == WM_MOUSEMOVE && t) {
        if (!t->hover) {
            t->hover = 1;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        {
            TRACKMOUSEEVENT tme;
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;
            TrackMouseEvent(&tme);
        }
    } else if (msg == WM_MOUSELEAVE && t) {
        if (t->hover) {
            t->hover = 0;
            InvalidateRect(hwnd, NULL, FALSE);
        }
    }

    return old ? CallWindowProcW(old, hwnd, msg, wParam, lParam)
               : DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void track_button(HWND hwnd)
{
    GbTrackedButton *t;
    if (!hwnd || g_tracked_count >= GB_MAX_TRACKED) return;
    t = &g_tracked[g_tracked_count++];
    t->hwnd = hwnd;
    t->hover = 0;
    t->old_proc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)TrackedButtonProc);
}

static int tracked_hover(HWND hwnd)
{
    GbTrackedButton *t = tracked_find(hwnd);
    return t ? t->hover : 0;
}

/* ------------------------------------------------------------------ fonts */

static void destroy_fonts(void)
{
    if (g_fontTitle) { DeleteObject(g_fontTitle); g_fontTitle = NULL; }
    if (g_fontBold) { DeleteObject(g_fontBold); g_fontBold = NULL; }
    if (g_fontNormal) { DeleteObject(g_fontNormal); g_fontNormal = NULL; }
    if (g_fontSmall) { DeleteObject(g_fontSmall); g_fontSmall = NULL; }
}

static HFONT make_font(int px, int weight)
{
    return CreateFontW(-S(px), 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_MODERN, L"Consolas");
}

static void create_fonts(void)
{
    destroy_fonts();
    g_fontTitle = make_font(17, FW_BOLD);
    g_fontBold = make_font(13, FW_BOLD);
    g_fontNormal = make_font(12, FW_NORMAL);
    g_fontSmall = make_font(11, FW_NORMAL);
}

static HBRUSH light_brush(void)
{
    if (!g_brushLight) g_brushLight = CreateSolidBrush(GB_LIGHTEST);
    return g_brushLight;
}

static HBRUSH dark_brush(void)
{
    if (!g_brushDark) g_brushDark = CreateSolidBrush(GB_DARK);
    return g_brushDark;
}

static void apply_control_fonts(void)
{
    HWND controls[] = {
        g_hListFiles, g_hCmbSlides, g_hChkBorder, g_hChkCover, g_hChkEnd,
        g_hEdtCourse, g_hEdtModule, g_hEdtAuthor, g_hEdtUni,
        g_hChkSubfolder, g_hEdtOutDir, g_hBtnBrowseOut, g_hBtnConvert,
        g_hBtnOpenFolder, g_hStatus, g_hLblOut, g_hLblSlides,
        g_hChkNote, g_hCmbNoteSide, g_hEdtNoteW, g_hEdtGap, g_hChkLines,
        g_hLblNoteSide, g_hLblNoteW, g_hLblNoteWUnit, g_hLblGap, g_hLblGapUnit,
        g_hLblCourse, g_hLblModule, g_hLblAuthor, g_hLblUni,
        g_hBtnMin, g_hBtnClose
    };
    int i;
    for (i = 0; i < (int)(sizeof(controls) / sizeof(controls[0])); i++) {
        if (controls[i]) SendMessageW(controls[i], WM_SETFONT, (WPARAM)g_fontNormal, TRUE);
    }
    if (g_hStatus) SendMessageW(g_hStatus, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
    {
        HWND labels[] = {
            g_hLblOut, g_hLblSlides, g_hLblNoteSide, g_hLblNoteW,
            g_hLblNoteWUnit, g_hLblGap, g_hLblGapUnit,
            g_hLblCourse, g_hLblModule, g_hLblAuthor, g_hLblUni
        };
        for (i = 0; i < (int)(sizeof(labels) / sizeof(labels[0])); i++) {
            if (labels[i]) SendMessageW(labels[i], WM_SETFONT, (WPARAM)g_fontSmall, TRUE);
        }
    }
}

/* --------------------------------------------------------------- helpers */

static void set_check_state(HWND hwnd, int checked)
{
    if (hwnd) {
        SendMessageW(hwnd, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
        InvalidateRect(hwnd, NULL, FALSE);
    }
}

static void sync_subfolder_state(void)
{
    EnableWindow(g_hEdtOutDir, !g_chk_subfolder);
    EnableWindow(g_hBtnBrowseOut, !g_chk_subfolder);
}

static void sync_note_state(void)
{
    BOOL on = g_note_enabled ? TRUE : FALSE;
    EnableWindow(g_hCmbNoteSide, on);
    EnableWindow(g_hEdtNoteW, on);
    EnableWindow(g_hChkLines, on);
    if (!on) {
        g_draw_note_lines = 0;
        set_check_state(g_hChkLines, 0);
    }
}

static void set_mm_edit(HWND hwnd, double value)
{
    wchar_t buf[32];
    _snwprintf(buf, 32, L"%.1f", value);
    buf[31] = L'\0';
    SetWindowTextW(hwnd, buf);
}

static double read_mm_edit(HWND hwnd, double def_val, double min_val, double max_val)
{
    wchar_t buf[32];
    wchar_t *end = NULL;
    double value;

    GetWindowTextW(hwnd, buf, 32);
    value = wcstod(buf, &end);
    if (end == buf || !isfinite(value)) value = def_val;
    if (value < min_val) value = min_val;
    if (value > max_val) value = max_val;
    set_mm_edit(hwnd, value);
    return value;
}

static void clear_input_files(void)
{
    int i;
    for (i = 0; i < g_input_count; i++) {
        if (g_input_files[i]) free(g_input_files[i]);
    }
    if (g_input_files) free(g_input_files);
    g_input_files = NULL;
    g_input_count = 0;
    g_input_capacity = 0;
    if (g_hListFiles) {
        SendMessageW(g_hListFiles, LB_RESETCONTENT, 0, 0);
    }
}

static void add_input_file(const wchar_t *path)
{
    int i;
    const wchar_t *fname;
    if (!path || !*path) return;
    for (i = 0; i < g_input_count; i++) {
        if (_wcsicmp(g_input_files[i], path) == 0) return;
    }
    if (g_input_count >= g_input_capacity) {
        int ncap = g_input_capacity ? g_input_capacity * 2 : 16;
        wchar_t **p = (wchar_t **)realloc(g_input_files, ncap * sizeof(wchar_t *));
        if (!p) return;
        g_input_files = p;
        g_input_capacity = ncap;
    }
    g_input_files[g_input_count++] = _wcsdup(path);

    fname = wcsrchr(path, L'\\');
    if (!fname) fname = wcsrchr(path, L'/');
    if (fname) fname++; else fname = path;

    if (g_hListFiles) {
        SendMessageW(g_hListFiles, LB_ADDSTRING, 0, (LPARAM)fname);
    }
}

static void set_status(const wchar_t *text)
{
    if (g_hStatus) SetWindowTextW(g_hStatus, text);
}

static void update_input_display(void)
{
    if (g_input_count == 0) {
        set_status(L"Ready. Select or drag PDF files to start.");
        EnableWindow(g_hBtnConvert, FALSE);
    } else if (g_input_count == 1) {
        set_status(L"1 file selected. Ready to convert.");
        EnableWindow(g_hBtnConvert, TRUE);
    } else {
        wchar_t buf[128];
        _snwprintf(buf, 128, L"%d files selected. Ready to convert.", g_input_count);
        set_status(buf);
        EnableWindow(g_hBtnConvert, TRUE);
    }
    InvalidateRect(g_hwnd, &g_rc_panel_files, FALSE);
}

static void auto_detect_names(const wchar_t *filepath)
{
    const wchar_t *fname;
    wchar_t name_no_ext[MAX_PATH];
    wchar_t *dot;
    wchar_t code[64] = { 0 };
    int i = 0, j = 0;

    if (!filepath || !*filepath) return;
    fname = wcsrchr(filepath, L'\\');
    if (!fname) fname = wcsrchr(filepath, L'/');
    if (fname) fname++; else fname = filepath;

    wcsncpy(name_no_ext, fname, MAX_PATH - 1);
    name_no_ext[MAX_PATH - 1] = L'\0';
    dot = wcsrchr(name_no_ext, L'.');
    if (dot) *dot = L'\0';

    SetWindowTextW(g_hEdtModule, name_no_ext);

    while (name_no_ext[i]) {
        if (iswalpha(name_no_ext[i])) {
            int start = i;
            int alpha_len;
            int num_start;
            int digit_len;
            while (iswalpha(name_no_ext[i])) i++;
            alpha_len = i - start;
            num_start = i;
            while (name_no_ext[i] == L' ') i++;
            while (iswdigit(name_no_ext[i])) i++;
            digit_len = i - num_start;
            if (alpha_len >= 2 && alpha_len <= 5 && digit_len >= 2 && digit_len <= 4) {
                wcsncpy(code, name_no_ext + start, (alpha_len + digit_len < 60) ? (alpha_len + digit_len) : 60);
                code[alpha_len + digit_len] = L'\0';
                for (j = 0; code[j]; j++) code[j] = towupper(code[j]);
                SetWindowTextW(g_hEdtCourse, code);
                break;
            }
        } else {
            i++;
        }
    }
}

/* ------------------------------------------------------------- file dialogs */

static void on_browse(void)
{
    IFileOpenDialog *pfd = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog, (void**)&pfd);
    if (FAILED(hr)) return;

    {
        FILEOPENDIALOGOPTIONS opt;
        COMDLG_FILTERSPEC filters[] = {
            { L"PDF Documents (*.pdf)", L"*.pdf" },
            { L"All Files (*.*)", L"*.*" }
        };
        IFileOpenDialog_GetOptions(pfd, &opt);
        IFileOpenDialog_SetOptions(pfd, opt | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
        IFileOpenDialog_SetTitle(pfd, L"Select PDF File(s)");
        IFileOpenDialog_SetFileTypes(pfd, 2, filters);

        if (SUCCEEDED(IFileOpenDialog_Show(pfd, g_hwnd))) {
            IShellItemArray *pItems = NULL;
            if (SUCCEEDED(IFileOpenDialog_GetResults(pfd, &pItems))) {
                DWORD numItems = 0;
                IShellItemArray_GetCount(pItems, &numItems);
                if (numItems > 0) {
                    DWORD i;
                    clear_input_files();
                    for (i = 0; i < numItems; i++) {
                        IShellItem *pItem = NULL;
                        if (SUCCEEDED(IShellItemArray_GetItemAt(pItems, i, &pItem))) {
                            LPWSTR pszFilePath = NULL;
                            if (SUCCEEDED(IShellItem_GetDisplayName(pItem, SIGDN_FILESYSPATH, &pszFilePath))) {
                                add_input_file(pszFilePath);
                                CoTaskMemFree(pszFilePath);
                            }
                            IShellItem_Release(pItem);
                        }
                    }
                    if (g_input_count > 0) auto_detect_names(g_input_files[0]);
                    update_input_display();
                }
                IShellItemArray_Release(pItems);
            }
        }
    }
    IFileOpenDialog_Release(pfd);
}

static void on_browse_output_dir(void)
{
    IFileOpenDialog *pfd = NULL;
    if (SUCCEEDED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog, (void**)&pfd))) {
        FILEOPENDIALOGOPTIONS opt;
        IFileOpenDialog_GetOptions(pfd, &opt);
        IFileOpenDialog_SetOptions(pfd, opt | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
        IFileOpenDialog_SetTitle(pfd, L"Select Output Folder");
        if (SUCCEEDED(IFileOpenDialog_Show(pfd, g_hwnd))) {
            IShellItem *pItem = NULL;
            if (SUCCEEDED(IFileOpenDialog_GetResult(pfd, &pItem))) {
                LPWSTR pszPath = NULL;
                if (SUCCEEDED(IShellItem_GetDisplayName(pItem, SIGDN_FILESYSPATH, &pszPath))) {
                    SetWindowTextW(g_hEdtOutDir, pszPath);
                    CoTaskMemFree(pszPath);
                }
                IShellItem_Release(pItem);
            }
        }
        IFileOpenDialog_Release(pfd);
    }
}

/* --------------------------------------------------------------- dropdown */

static void on_slides_picked(int index, void *user)
{
    (void)user;
    if (index < 0 || index > 2) index = 0;
    g_slides_sel = index;
    SetWindowTextW(g_hCmbSlides, g_slide_items[g_slides_sel]);
    InvalidateRect(g_hCmbSlides, NULL, FALSE);
}

static void open_slides_dropdown(void)
{
    RECT anchor;
    GetWindowRect(g_hCmbSlides, &anchor);
    MapWindowPoints(NULL, g_hwnd, (POINT*)&anchor, 2);
    gb_dropdown_open(g_hwnd, &anchor, g_slide_items, 3, g_slides_sel, on_slides_picked, NULL);
}

static void on_note_side_picked(int index, void *user)
{
    (void)user;
    if (index < 0 || index > 1) index = 0;
    g_note_side = index;
    SetWindowTextW(g_hCmbNoteSide, g_note_side_items[g_note_side]);
    InvalidateRect(g_hCmbNoteSide, NULL, FALSE);
}

static void open_note_side_dropdown(void)
{
    RECT anchor;
    GetWindowRect(g_hCmbNoteSide, &anchor);
    MapWindowPoints(NULL, g_hwnd, (POINT*)&anchor, 2);
    gb_dropdown_open(g_hwnd, &anchor, g_note_side_items, 2, g_note_side, on_note_side_picked, NULL);
}

/* --------------------------------------------------------------- progress */

typedef struct {
    int file_index;
    int file_count;
} ProgressCtx;

static void on_progress(int current_slide, int total_slides, void *user_data)
{
    ProgressCtx *ctx = (ProgressCtx*)user_data;
    int pct = 0;
    if (ctx && ctx->file_count > 0 && total_slides > 0) {
        int file_pct = current_slide * 100 / total_slides;
        pct = ((ctx->file_index * 100) + file_pct) / ctx->file_count;
    }
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    g_progress = pct;
    InvalidateRect(g_hwnd, &g_rc_progress, FALSE);
    UpdateWindow(g_hwnd);
}

/* --------------------------------------------------------------- convert */

static void do_convert(void)
{
    int use_subfolder, total_converted = 0, total_slides_all = 0, total_pages_all = 0;
    wchar_t custom_out_dir[MAX_PATH] = { 0 };
    PDFEngineConfig cfg;
    char course_u8[256] = { 0 };
    char module_u8[256] = { 0 };
    char author_u8[256] = { 0 };
    char uni_u8[256] = { 0 };
    wchar_t tmp_w[256];
    int f_idx;

    if (g_input_count == 0) {
        gb_message_box(g_hwnd, L"WARNING", L"Please select or drag at least one PDF file first.", MB_OK | MB_ICONWARNING);
        return;
    }

    EnableWindow(g_hBtnConvert, FALSE);
    ShowWindow(g_hBtnOpenFolder, SW_HIDE);
    g_progress = 0;
    InvalidateRect(g_hwnd, &g_rc_progress, FALSE);
    UpdateWindow(g_hwnd);

    use_subfolder = g_chk_subfolder;
    GetWindowTextW(g_hEdtOutDir, custom_out_dir, MAX_PATH);

    pdf_engine_config_init(&cfg);
    g_note_width_mm = read_mm_edit(g_hEdtNoteW, 84.0, 30.0, 145.0);
    g_slide_gap_mm = read_mm_edit(g_hEdtGap, 5.0, 0.0, 40.0);

    if (g_slides_sel == 1) cfg.slides_per_page = 2;
    else if (g_slides_sel == 2) cfg.slides_per_page = 4;
    else cfg.slides_per_page = 3;

    cfg.draw_note_lines = g_note_enabled ? g_draw_note_lines : 0;
    cfg.draw_border = g_chk_border;
    cfg.add_cover = g_chk_cover;
    cfg.add_end = g_chk_end;
    cfg.note_area_enabled = g_note_enabled;
    cfg.note_side = g_note_side;
    cfg.note_width_mm = g_note_width_mm;
    cfg.slide_gap_mm = g_slide_gap_mm;

    GetWindowTextW(g_hEdtCourse, tmp_w, 256);
    WideCharToMultiByte(CP_UTF8, 0, tmp_w, -1, course_u8, sizeof(course_u8), NULL, NULL);
    GetWindowTextW(g_hEdtModule, tmp_w, 256);
    WideCharToMultiByte(CP_UTF8, 0, tmp_w, -1, module_u8, sizeof(module_u8), NULL, NULL);
    GetWindowTextW(g_hEdtAuthor, tmp_w, 256);
    WideCharToMultiByte(CP_UTF8, 0, tmp_w, -1, author_u8, sizeof(author_u8), NULL, NULL);
    GetWindowTextW(g_hEdtUni, tmp_w, 256);
    WideCharToMultiByte(CP_UTF8, 0, tmp_w, -1, uni_u8, sizeof(uni_u8), NULL, NULL);

    cfg.course_name = course_u8;
    cfg.module_name = module_u8;
    cfg.author_name = author_u8;
    cfg.university = uni_u8;

    for (f_idx = 0; f_idx < g_input_count; f_idx++) {
        const wchar_t *in_path = g_input_files[f_idx];
        const wchar_t *fname = wcsrchr(in_path, L'\\');
        wchar_t out_dir[MAX_PATH] = { 0 };
        wchar_t fname_no_ext[MAX_PATH];
        wchar_t out_file[MAX_PATH];
        wchar_t status_msg[256];
        wchar_t *dot;
        int out_pages = 0, out_slides = 0;
        char err_msg[512] = { 0 };
        ProgressCtx ctx;
        int res;

        if (!fname) fname = wcsrchr(in_path, L'/');
        if (fname) fname++; else fname = in_path;

        _snwprintf(status_msg, 256, L"[%d/%d] Converting: %ls", f_idx + 1, g_input_count, fname);
        set_status(status_msg);
        UpdateWindow(g_hwnd);

        if (use_subfolder) {
            const wchar_t *slash = wcsrchr(in_path, L'\\');
            if (!slash) slash = wcsrchr(in_path, L'/');
            if (slash) {
                wcsncpy(out_dir, in_path, slash - in_path);
                out_dir[slash - in_path] = L'\0';
                wcsncat(out_dir, L"\\_formatted", MAX_PATH - wcslen(out_dir) - 1);
            } else {
                wcsncpy(out_dir, L"_formatted", MAX_PATH - 1);
            }
        } else if (custom_out_dir[0]) {
            wcsncpy(out_dir, custom_out_dir, MAX_PATH - 1);
        } else {
            const wchar_t *slash = wcsrchr(in_path, L'\\');
            if (!slash) slash = wcsrchr(in_path, L'/');
            if (slash) {
                wcsncpy(out_dir, in_path, slash - in_path);
                out_dir[slash - in_path] = L'\0';
            } else {
                GetCurrentDirectoryW(MAX_PATH, out_dir);
            }
        }

        CreateDirectoryW(out_dir, NULL);
        wcsncpy(g_last_output_dir, out_dir, MAX_PATH - 1);

        wcsncpy(fname_no_ext, fname, MAX_PATH - 1);
        fname_no_ext[MAX_PATH - 1] = L'\0';
        dot = wcsrchr(fname_no_ext, L'.');
        if (dot) *dot = L'\0';

        _snwprintf(out_file, MAX_PATH, L"%ls\\%ls_formatted.pdf", out_dir, fname_no_ext);

        ctx.file_index = f_idx;
        ctx.file_count = g_input_count;

        res = process_pdf_file_w(in_path, out_file, &cfg, on_progress, &ctx, &out_pages, &out_slides, err_msg, sizeof(err_msg));
        if (res == 0) {
            total_converted++;
            total_slides_all += out_slides;
            total_pages_all += out_pages;
        } else {
            wchar_t err_w[512] = { 0 };
            MultiByteToWideChar(CP_UTF8, 0, err_msg, -1, err_w, 512);
            err_w[511] = L'\0';
            gb_message_box(g_hwnd, L"ERROR", err_w[0] ? err_w : L"The PDF could not be converted.", MB_OK | MB_ICONERROR);
        }
    }

    g_progress = 100;
    InvalidateRect(g_hwnd, &g_rc_progress, FALSE);
    UpdateWindow(g_hwnd);
    EnableWindow(g_hBtnConvert, TRUE);

    if (total_converted > 0) {
        wchar_t done_msg[256];
        wchar_t success_dialog[512];
        _snwprintf(done_msg, 256, L"Done! %d file(s) converted (%d slides -> %d pages).",
                   total_converted, total_slides_all, total_pages_all);
        set_status(done_msg);
        ShowWindow(g_hBtnOpenFolder, SW_SHOW);
        EnableWindow(g_hBtnOpenFolder, TRUE);

        _snwprintf(success_dialog, 512,
            L"Conversion completed successfully.\n\n"
            L"Files converted: %d\n"
            L"Total slides: %d\n"
            L"Total pages: %d\n\n"
            L"Output folder:\n%ls",
            total_converted, total_slides_all, total_pages_all, g_last_output_dir);
        gb_message_box(g_hwnd, L"SUCCESS", success_dialog, MB_OK | MB_ICONINFORMATION);
    }
}

/* ---------------------------------------------------------------- drawing */

static void draw_panel(HDC hdc, const RECT *prc, const wchar_t *title)
{
    RECT tab;
    RECT tr;
    int tab_h = S(20);

    gb_frame_round(hdc, prc, GB_DARKEST, S(2), GB_DARK, GB_LIGHTEST, 1);
    tab.left = prc->left + S(2);
    tab.top = prc->top + S(2);
    tab.right = prc->right - S(2);
    tab.bottom = tab.top + tab_h;
    gb_fill(hdc, &tab, GB_DARKEST);
    tr = tab;
    tr.left += S(8);
    tr.right -= S(8);
    gb_text(hdc, g_fontBold, GB_LIGHTEST, &tr, title,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

static void draw_control_frame(HDC hdc, HWND ctl)
{
    RECT rc;
    if (!ctl) return;
    GetWindowRect(ctl, &rc);
    MapWindowPoints(NULL, g_hwnd, (POINT*)&rc, 2);
    InflateRect(&rc, S(2), S(2));
    gb_frame_round(hdc, &rc, GB_DARKEST, S(2), GB_LIGHTEST, GB_LIGHTEST, 0);
}

static void draw_title_icon(HDC hdc, int x, int y, int s)
{
    RECT r;

    r.left = x; r.top = y + s; r.right = x + 10 * s; r.bottom = y + 13 * s;
    gb_fill(hdc, &r, GB_LIGHTEST);

    r.left = x + 7 * s; r.top = y + s; r.right = x + 10 * s; r.bottom = y + 4 * s;
    gb_fill(hdc, &r, GB_LIGHT);

    r.left = x + 2 * s; r.top = y + 4 * s; r.right = x + 8 * s; r.bottom = y + 5 * s;
    gb_fill(hdc, &r, GB_DARKEST);
    r.left = x + 2 * s; r.top = y + 6 * s; r.right = x + 8 * s; r.bottom = y + 7 * s;
    gb_fill(hdc, &r, GB_DARKEST);
    r.left = x + 2 * s; r.top = y + 8 * s; r.right = x + 6 * s; r.bottom = y + 9 * s;
    gb_fill(hdc, &r, GB_DARKEST);
}

static void draw_progress_bar(HDC hdc, const RECT *prc, int percent)
{
    RECT inner;
    int inner_w, fill_w, x;
    int block, gap;

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    gb_fill(hdc, prc, GB_LIGHT);
    gb_frame_round(hdc, prc, GB_DARKEST, S(2), GB_DARK, GB_LIGHT, 0);

    inner = *prc;
    InflateRect(&inner, -S(3), -S(3));
    inner_w = inner.right - inner.left;
    if (inner_w <= 0) return;

    fill_w = MulDiv(inner_w, percent, 100);
    block = S(7);
    gap = S(2);
    if (block < 2) block = 2;
    if (gap < 1) gap = 1;

    for (x = block + gap; x < inner_w; x += block + gap) {
        RECT s;
        s.left = inner.left + x - gap;
        s.top = inner.top;
        s.right = s.left + 1;
        s.bottom = inner.bottom;
        gb_fill(hdc, &s, GB_DARK);
    }

    for (x = 0; x < fill_w; x += block + gap) {
        RECT b;
        b.left = inner.left + x;
        b.top = inner.top;
        b.right = b.left + block;
        if (b.right > inner.left + fill_w) b.right = inner.left + fill_w;
        b.bottom = inner.bottom;
        if (b.right > b.left) gb_fill(hdc, &b, GB_DARKEST);
    }
}

static void draw_list_item(const DRAWITEMSTRUCT *dis)
{
    wchar_t name[512] = L"";
    RECT rc;
    RECT tr;

    if (!dis) return;
    rc = dis->rcItem;

    if (dis->itemID == (UINT)-1) {
        gb_fill(dis->hDC, &rc, GB_LIGHTEST);
        return;
    }

    SendMessageW(g_hListFiles, LB_GETTEXT, dis->itemID, (LPARAM)name);
    name[511] = L'\0';

    if (dis->itemState & ODS_SELECTED) {
        gb_fill(dis->hDC, &rc, GB_DARKEST);
        tr = rc;
        tr.left += S(8);
        tr.right -= S(8);
        {
            RECT mark;
            mark.left = rc.left + S(4);
            mark.top = rc.top + (rc.bottom - rc.top) / 2 - S(2);
            mark.right = mark.left + S(2);
            mark.bottom = mark.top + S(4);
            gb_fill(dis->hDC, &mark, GB_LIGHTEST);
            tr.left += S(6);
        }
        gb_text(dis->hDC, g_fontNormal, GB_LIGHTEST, &tr, name,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    } else {
        gb_fill(dis->hDC, &rc, GB_LIGHTEST);
        tr = rc;
        tr.left += S(10);
        tr.right -= S(8);
        gb_text(dis->hDC, g_fontNormal, GB_DARKEST, &tr, name,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
}

static void paint_main(HWND hwnd, HDC hdc)
{
    RECT rc;
    RECT inner;
    RECT title;
    RECT tr;
    RECT sub;
    RECT sep;
    wchar_t files_title[64];

    GetClientRect(hwnd, &rc);
    gb_fill(hdc, &rc, GB_DARK);
    gb_frame(hdc, &rc, GB_DARKEST, S(6));
    inner = rc;
    InflateRect(&inner, -S(6), -S(6));
    gb_frame(hdc, &inner, GB_LIGHTEST, S(1));

    title = rc;
    title.bottom = S(44);
    gb_fill(hdc, &title, GB_DARKEST);

    draw_title_icon(hdc, S(16), S(10), 2);

    tr.left = S(46);
    tr.top = S(6);
    tr.right = rc.right - S(120);
    tr.bottom = S(30);
    gb_text(hdc, g_fontTitle, GB_LIGHTEST, &tr, L"SLIDE NOTE FORMATTER",
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    sub.left = S(47);
    sub.top = S(27);
    sub.right = rc.right - S(120);
    sub.bottom = S(42);
    gb_text(hdc, g_fontSmall, GB_LIGHT, &sub, L"NATIVE C / WIN32",
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    sep.left = S(12);
    sep.top = S(44);
    sep.right = rc.right - S(12);
    sep.bottom = S(45);
    gb_fill(hdc, &sep, GB_LIGHT);

    _snwprintf(files_title, 64, L"1  FILES (%d)", g_input_count);
    draw_panel(hdc, &g_rc_panel_files, files_title);
    draw_panel(hdc, &g_rc_panel_output, L"2  OUTPUT");
    draw_panel(hdc, &g_rc_panel_layout, L"3  LAYOUT");
    draw_panel(hdc, &g_rc_panel_cover, L"4  COVER & DETAILS");

    draw_control_frame(hdc, g_hListFiles);
    draw_control_frame(hdc, g_hEdtOutDir);
    draw_control_frame(hdc, g_hEdtNoteW);
    draw_control_frame(hdc, g_hEdtGap);
    draw_control_frame(hdc, g_hEdtCourse);
    draw_control_frame(hdc, g_hEdtModule);
    draw_control_frame(hdc, g_hEdtAuthor);
    draw_control_frame(hdc, g_hEdtUni);

    draw_progress_bar(hdc, &g_rc_progress, g_progress);
}

/* --------------------------------------------------------------- layout */

static void layout_controls(void)
{
    RECT rc;
    int right = BASE_CLIENT_W;

    if (!g_hwnd) return;

    g_rc_panel_files.left = S(20);
    g_rc_panel_files.top = S(54);
    g_rc_panel_files.right = S(right - 20);
    g_rc_panel_files.bottom = S(218);

    g_rc_panel_output.left = S(20);
    g_rc_panel_output.top = S(228);
    g_rc_panel_output.right = S(right - 20);
    g_rc_panel_output.bottom = S(318);

    g_rc_panel_layout.left = S(20);
    g_rc_panel_layout.top = S(328);
    g_rc_panel_layout.right = S(right - 20);
    g_rc_panel_layout.bottom = S(408);

    g_rc_panel_cover.left = S(20);
    g_rc_panel_cover.top = S(418);
    g_rc_panel_cover.right = S(right - 20);
    g_rc_panel_cover.bottom = S(572);

    g_rc_progress.left = S(20);
    g_rc_progress.top = S(638);
    g_rc_progress.right = S(right - 20);
    g_rc_progress.bottom = S(648);

    SetWindowPos(g_hListFiles, NULL, S(32), S(84), S(416), S(122), SWP_NOZORDER);
    SetWindowPos(g_hBtnBrowse, NULL, S(460), S(84), S(154), S(34), SWP_NOZORDER);
    SetWindowPos(g_hBtnClear, NULL, S(460), S(126), S(154), S(34), SWP_NOZORDER);

    SetWindowPos(g_hChkSubfolder, NULL, S(32), S(252), S(500), S(24), SWP_NOZORDER);
    SetWindowPos(g_hLblOut, NULL, S(32), S(286), S(104), S(20), SWP_NOZORDER);
    SetWindowPos(g_hEdtOutDir, NULL, S(140), S(282), S(348), S(26), SWP_NOZORDER);
    SetWindowPos(g_hBtnBrowseOut, NULL, S(498), S(280), S(116), S(28), SWP_NOZORDER);

    SetWindowPos(g_hLblSlides, NULL, S(32), S(354), S(70), S(20), SWP_NOZORDER);
    SetWindowPos(g_hCmbSlides, NULL, S(105), S(350), S(140), S(28), SWP_NOZORDER);
    SetWindowPos(g_hChkBorder, NULL, S(255), S(352), S(145), S(24), SWP_NOZORDER);
    SetWindowPos(g_hChkNote, NULL, S(410), S(352), S(150), S(24), SWP_NOZORDER);

    SetWindowPos(g_hLblNoteSide, NULL, S(32), S(384), S(36), S(20), SWP_NOZORDER);
    SetWindowPos(g_hCmbNoteSide, NULL, S(72), S(380), S(90), S(26), SWP_NOZORDER);
    SetWindowPos(g_hLblNoteW, NULL, S(178), S(384), S(48), S(20), SWP_NOZORDER);
    SetWindowPos(g_hEdtNoteW, NULL, S(230), S(380), S(56), S(26), SWP_NOZORDER);
    SetWindowPos(g_hLblNoteWUnit, NULL, S(290), S(384), S(24), S(20), SWP_NOZORDER);
    SetWindowPos(g_hLblGap, NULL, S(322), S(384), S(34), S(20), SWP_NOZORDER);
    SetWindowPos(g_hEdtGap, NULL, S(360), S(380), S(56), S(26), SWP_NOZORDER);
    SetWindowPos(g_hLblGapUnit, NULL, S(420), S(384), S(24), S(20), SWP_NOZORDER);
    SetWindowPos(g_hChkLines, NULL, S(455), S(380), S(150), S(24), SWP_NOZORDER);

    SetWindowPos(g_hChkCover, NULL, S(32), S(440), S(200), S(24), SWP_NOZORDER);
    SetWindowPos(g_hChkEnd, NULL, S(250), S(440), S(180), S(24), SWP_NOZORDER);

    SetWindowPos(g_hLblCourse, NULL, S(32), S(476), S(112), S(20), SWP_NOZORDER);
    SetWindowPos(g_hEdtCourse, NULL, S(146), S(472), S(180), S(26), SWP_NOZORDER);
    SetWindowPos(g_hLblModule, NULL, S(342), S(476), S(84), S(20), SWP_NOZORDER);
    SetWindowPos(g_hEdtModule, NULL, S(430), S(472), S(184), S(26), SWP_NOZORDER);

    SetWindowPos(g_hLblAuthor, NULL, S(32), S(514), S(112), S(20), SWP_NOZORDER);
    SetWindowPos(g_hEdtAuthor, NULL, S(146), S(510), S(180), S(26), SWP_NOZORDER);
    SetWindowPos(g_hLblUni, NULL, S(342), S(514), S(84), S(20), SWP_NOZORDER);
    SetWindowPos(g_hEdtUni, NULL, S(430), S(510), S(184), S(26), SWP_NOZORDER);

    SetWindowPos(g_hBtnConvert, NULL, S(20), S(588), S(450), S(40), SWP_NOZORDER);
    SetWindowPos(g_hBtnOpenFolder, NULL, S(480), S(588), S(146), S(40), SWP_NOZORDER);
    SetWindowPos(g_hStatus, NULL, S(22), S(652), S(602), S(18), SWP_NOZORDER);

    SetWindowPos(g_hBtnClose, NULL, S(right - 40), S(9), S(26), S(24), SWP_NOZORDER);
    SetWindowPos(g_hBtnMin, NULL, S(right - 72), S(9), S(26), S(24), SWP_NOZORDER);

    GetClientRect(g_hwnd, &rc);
    gb_round_window(g_hwnd, 1);
    InvalidateRect(g_hwnd, NULL, TRUE);
}

/* --------------------------------------------------------------- controls */

static HWND make_button(HWND parent, HINSTANCE inst, int id, const wchar_t *text,
                        DWORD extra_style)
{
    return CreateWindowW(L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | extra_style,
        0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, inst, NULL);
}

static HWND make_check(HWND parent, HINSTANCE inst, int id, const wchar_t *text)
{
    HWND h = CreateWindowW(L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, inst, NULL);
    track_button(h);
    return h;
}

static HWND make_edit(HWND parent, HINSTANCE inst, int id)
{
    return CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, inst, NULL);
}

static HWND make_label(HWND parent, HINSTANCE inst, const wchar_t *text)
{
    return CreateWindowW(L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
        0, 0, 10, 10, parent, NULL, inst, NULL);
}

static void create_controls(HWND hwnd, HINSTANCE inst)
{
    g_hListFiles = CreateWindowW(L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_OWNERDRAWFIXED |
        LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
        0, 0, 10, 10, hwnd, (HMENU)ID_LST_FILES, inst, NULL);
    SendMessageW(g_hListFiles, LB_SETITEMHEIGHT, 0, S(24));

    g_hBtnBrowse = make_button(hwnd, inst, ID_BTN_BROWSE, L"BROWSE...", 0);
    g_hBtnClear = make_button(hwnd, inst, ID_BTN_CLEAR, L"CLEAR", 0);
    track_button(g_hBtnBrowse);
    track_button(g_hBtnClear);

    g_hChkSubfolder = make_check(hwnd, inst, ID_CHK_SUBFOLDER, L"SAVE TO SUBFOLDER (_formatted)");
    g_hLblOut = make_label(hwnd, inst, L"CUSTOM FOLDER:");
    g_hEdtOutDir = make_edit(hwnd, inst, ID_EDT_OUTDIR);
    g_hBtnBrowseOut = make_button(hwnd, inst, ID_BTN_BROWSE_OUT, L"...", 0);
    track_button(g_hBtnBrowseOut);

    g_hLblSlides = make_label(hwnd, inst, L"SLIDES:");
    g_hCmbSlides = make_button(hwnd, inst, ID_CMB_SLIDES, g_slide_items[0], 0);
    track_button(g_hCmbSlides);
    g_hChkBorder = make_check(hwnd, inst, ID_CHK_BORDER, L"SLIDE BORDER");

    g_hChkNote = make_check(hwnd, inst, ID_CHK_NOTE, L"NOTE COLUMN");
    g_hLblNoteSide = make_label(hwnd, inst, L"SIDE:");
    g_hCmbNoteSide = make_button(hwnd, inst, ID_CMB_NOTE_SIDE, g_note_side_items[g_note_side], 0);
    track_button(g_hCmbNoteSide);
    g_hLblNoteW = make_label(hwnd, inst, L"WIDTH:");
    g_hEdtNoteW = make_edit(hwnd, inst, ID_EDT_NOTE_W);
    g_hLblNoteWUnit = make_label(hwnd, inst, L"MM");
    g_hLblGap = make_label(hwnd, inst, L"GAP:");
    g_hEdtGap = make_edit(hwnd, inst, ID_EDT_GAP);
    g_hLblGapUnit = make_label(hwnd, inst, L"MM");
    g_hChkLines = make_check(hwnd, inst, ID_CHK_LINES, L"RULED LINES");

    g_hChkCover = make_check(hwnd, inst, ID_CHK_COVER, L"FRONT COVER PAGE");
    g_hChkEnd = make_check(hwnd, inst, ID_CHK_END, L"END PAGE");

    g_hLblCourse = make_label(hwnd, inst, L"COURSE / TITLE:");
    g_hEdtCourse = make_edit(hwnd, inst, ID_EDT_COURSE);
    g_hLblModule = make_label(hwnd, inst, L"SUBTITLE:");
    g_hEdtModule = make_edit(hwnd, inst, ID_EDT_MODULE);

    g_hLblAuthor = make_label(hwnd, inst, L"STUDENT NAME:");
    g_hEdtAuthor = make_edit(hwnd, inst, ID_EDT_AUTHOR);
    g_hLblUni = make_label(hwnd, inst, L"INSTITUTION:");
    g_hEdtUni = make_edit(hwnd, inst, ID_EDT_UNI);

    g_hBtnConvert = make_button(hwnd, inst, ID_BTN_CONVERT, L"CONVERT PDFS", 0);
    track_button(g_hBtnConvert);

    g_hBtnOpenFolder = CreateWindowW(L"BUTTON", L"OPEN FOLDER",
        WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 10, 10, hwnd, (HMENU)ID_BTN_OPEN_FOLDER, inst, NULL);
    track_button(g_hBtnOpenFolder);

    g_hStatus = CreateWindowW(L"STATIC", L"Ready. Select or drag PDF files to start.",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS,
        0, 0, 10, 10, hwnd, NULL, inst, NULL);

    g_hBtnMin = CreateWindowW(L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 10, 10, hwnd, (HMENU)ID_BTN_MIN, inst, NULL);
    g_hBtnClose = CreateWindowW(L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, 10, 10, hwnd, (HMENU)ID_BTN_CLOSE, inst, NULL);
    track_button(g_hBtnMin);
    track_button(g_hBtnClose);

    set_check_state(g_hChkSubfolder, g_chk_subfolder);
    set_check_state(g_hChkBorder, g_chk_border);
    set_check_state(g_hChkCover, g_chk_cover);
    set_check_state(g_hChkEnd, g_chk_end);
    set_check_state(g_hChkNote, g_note_enabled);
    set_check_state(g_hChkLines, g_draw_note_lines);
    SetWindowTextW(g_hCmbSlides, g_slide_items[g_slides_sel]);
    SetWindowTextW(g_hCmbNoteSide, g_note_side_items[g_note_side]);
    set_mm_edit(g_hEdtNoteW, g_note_width_mm);
    set_mm_edit(g_hEdtGap, g_slide_gap_mm);
    sync_subfolder_state();
    sync_note_state();
    update_input_display();
    EnableWindow(g_hBtnConvert, FALSE);

    apply_control_fonts();
    layout_controls();
    SetFocus(g_hListFiles);
}

/* --------------------------------------------------------------- window */

static UINT query_system_dpi(void)
{
    HDC dc = GetDC(NULL);
    UINT dpi = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(NULL, dc);
    return dpi ? dpi : 96;
}

static UINT query_window_dpi(HWND hwnd)
{
    typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
    static GetDpiForWindowFn fn = NULL;
    static int loaded = 0;
    if (!loaded) {
        fn = (GetDpiForWindowFn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
        loaded = 1;
    }
    if (fn) {
        UINT dpi = fn(hwnd);
        if (dpi) return dpi;
    }
    return query_system_dpi();
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE inst = ((LPCREATESTRUCT)lParam)->hInstance;
            g_hwnd = hwnd;
            g_dpi = query_window_dpi(hwnd);
            gb_set_dpi(g_dpi);
            create_fonts();
            DragAcceptFiles(hwnd, TRUE);
            create_controls(hwnd, inst);
            return 0;
        }

        case WM_DPICHANGED: {
            RECT *suggested = (RECT*)lParam;
            g_dpi = HIWORD(wParam);
            if (g_dpi == 0) g_dpi = query_window_dpi(hwnd);
            gb_set_dpi(g_dpi);
            create_fonts();
            apply_control_fonts();
            if (suggested) {
                SetWindowPos(hwnd, NULL, suggested->left, suggested->top,
                             S(BASE_CLIENT_W), S(BASE_CLIENT_H),
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            layout_controls();
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            paint_main(hwnd, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            SetBkMode(hdc, TRANSPARENT);
            if ((HWND)lParam == g_hStatus) {
                SetTextColor(hdc, GB_LIGHTEST);
                return (LRESULT)dark_brush();
            }
            SetTextColor(hdc, GB_DARKEST);
            return (LRESULT)light_brush();
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, GB_DARKEST);
            SetBkColor(hdc, GB_LIGHTEST);
            return (LRESULT)light_brush();
        }

        case WM_CTLCOLORLISTBOX: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, GB_DARKEST);
            SetBkColor(hdc, GB_LIGHTEST);
            return (LRESULT)light_brush();
        }

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT*)lParam;
            int role = GB_ROLE_PUSH;
            int checked = 0;
            HFONT font = g_fontNormal;

            if (!dis) break;

            if (dis->CtlType == ODT_LISTBOX) {
                draw_list_item(dis);
                return TRUE;
            }

            if (dis->CtlID == ID_BTN_CONVERT) {
                role = GB_ROLE_PRIMARY;
                font = g_fontBold;
            } else if (dis->CtlID == ID_BTN_MIN) {
                role = GB_ROLE_TITLE;
                checked = 0;
            } else if (dis->CtlID == ID_BTN_CLOSE) {
                role = GB_ROLE_TITLE;
                checked = 1;
            } else if (dis->CtlID == ID_CMB_SLIDES) {
                role = GB_ROLE_DROP;
            } else if (dis->CtlID == ID_CMB_NOTE_SIDE) {
                role = GB_ROLE_DROP;
            } else if (dis->CtlID == ID_CHK_NOTE) {
                role = GB_ROLE_CHECK;
                checked = g_note_enabled;
            } else if (dis->CtlID == ID_CHK_LINES) {
                role = GB_ROLE_CHECK;
                checked = g_draw_note_lines;
            } else if (dis->CtlID == ID_CHK_SUBFOLDER) {
                role = GB_ROLE_CHECK;
                checked = g_chk_subfolder;
            } else if (dis->CtlID == ID_CHK_BORDER) {
                role = GB_ROLE_CHECK;
                checked = g_chk_border;
            } else if (dis->CtlID == ID_CHK_COVER) {
                role = GB_ROLE_CHECK;
                checked = g_chk_cover;
            } else if (dis->CtlID == ID_CHK_END) {
                role = GB_ROLE_CHECK;
                checked = g_chk_end;
            }

            gb_draw_control(dis, font, role, checked, tracked_hover(dis->hwndItem));
            return TRUE;
        }

        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
            if (count > 0) {
                UINT i;
                clear_input_files();
                for (i = 0; i < count; i++) {
                    wchar_t szFile[MAX_PATH];
                    if (DragQueryFileW(hDrop, i, szFile, MAX_PATH)) {
                        const wchar_t *ext = wcsrchr(szFile, L'.');
                        if (ext && _wcsicmp(ext, L".pdf") == 0) {
                            add_input_file(szFile);
                        }
                    }
                }
                if (g_input_count > 0) auto_detect_names(g_input_files[0]);
                update_input_display();
            }
            DragFinish(hDrop);
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id == ID_BTN_BROWSE) {
                on_browse();
            } else if (id == ID_BTN_CLEAR) {
                clear_input_files();
                update_input_display();
            } else if (id == ID_CHK_SUBFOLDER) {
                g_chk_subfolder = !g_chk_subfolder;
                set_check_state(g_hChkSubfolder, g_chk_subfolder);
                sync_subfolder_state();
            } else if (id == ID_CHK_BORDER) {
                g_chk_border = !g_chk_border;
                set_check_state(g_hChkBorder, g_chk_border);
            } else if (id == ID_CHK_COVER) {
                g_chk_cover = !g_chk_cover;
                set_check_state(g_hChkCover, g_chk_cover);
            } else if (id == ID_CHK_END) {
                g_chk_end = !g_chk_end;
                set_check_state(g_hChkEnd, g_chk_end);
            } else if (id == ID_CHK_NOTE) {
                g_note_enabled = !g_note_enabled;
                set_check_state(g_hChkNote, g_note_enabled);
                sync_note_state();
            } else if (id == ID_CHK_LINES) {
                g_draw_note_lines = !g_draw_note_lines;
                set_check_state(g_hChkLines, g_draw_note_lines);
            } else if (id == ID_CMB_NOTE_SIDE) {
                open_note_side_dropdown();
            } else if (id == ID_CMB_SLIDES) {
                open_slides_dropdown();
            } else if (id == ID_BTN_BROWSE_OUT) {
                on_browse_output_dir();
            } else if (id == ID_BTN_CONVERT) {
                do_convert();
            } else if (id == ID_BTN_OPEN_FOLDER) {
                if (g_last_output_dir[0]) {
                    ShellExecuteW(hwnd, L"open", g_last_output_dir, NULL, NULL, SW_SHOWNORMAL);
                }
            } else if (id == ID_BTN_MIN) {
                ShowWindow(hwnd, SW_MINIMIZE);
            } else if (id == ID_BTN_CLOSE) {
                DestroyWindow(hwnd);
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int y = GET_Y_LPARAM(lParam);
            if (y < S(44)) {
                ReleaseCapture();
                SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                return 0;
            }
            break;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            clear_input_files();
            destroy_fonts();
            if (g_brushLight) { DeleteObject(g_brushLight); g_brushLight = NULL; }
            if (g_brushDark) { DeleteObject(g_brushDark); g_brushDark = NULL; }
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    int argc = 0;
    LPWSTR *argv;
    (void)hPrevInstance;
    (void)lpCmdLine;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    if (argc > 1) {
        if (wcscmp(argv[1], L"-h") == 0 || wcscmp(argv[1], L"--help") == 0 || wcscmp(argv[1], L"/?") == 0) {
            MessageBoxW(NULL,
                L"Slide Note Formatter 2.1.2 (Native C / Win32)\n\n"
                L"Usage (CLI):\n"
                L"  SlideNoteFormatter.exe <input.pdf> [output.pdf] [slides_per_page] [-author \"Name\"]\n\n"
                L"Example:\n"
                L"  SlideNoteFormatter.exe lecture.pdf output.pdf 3 -author \"Haluk Tanriseven\"\n\n"
                L"Or simply drag and drop PDF files onto the executable icon.",
                L"CLI Help", MB_ICONINFORMATION);
            LocalFree(argv);
            CoUninitialize();
            return 0;
        }

        {
            wchar_t *in_arg = NULL;
            wchar_t sz_out_buf[MAX_PATH] = { 0 };
            wchar_t *out_arg = NULL;
            int slides = 3;
            wchar_t *author_arg = NULL;
            wchar_t *course_arg = NULL;
            wchar_t *module_arg = NULL;
            wchar_t *uni_arg = NULL;
            int i;

            for (i = 1; i < argc; i++) {
                if (wcscmp(argv[i], L"-author") == 0 && i + 1 < argc) {
                    author_arg = argv[++i];
                } else if (wcscmp(argv[i], L"-course") == 0 && i + 1 < argc) {
                    course_arg = argv[++i];
                } else if (wcscmp(argv[i], L"-module") == 0 && i + 1 < argc) {
                    module_arg = argv[++i];
                } else if (wcscmp(argv[i], L"-uni") == 0 && i + 1 < argc) {
                    uni_arg = argv[++i];
                } else if (wcscmp(argv[i], L"-slides") == 0 && i + 1 < argc) {
                    slides = _wtoi(argv[++i]);
                } else if (!in_arg) {
                    in_arg = argv[i];
                } else if (!out_arg) {
                    out_arg = argv[i];
                } else {
                    int s = _wtoi(argv[i]);
                    if (s >= 2 && s <= 4) slides = s;
                }
            }

            if (!in_arg) {
                LocalFree(argv);
                CoUninitialize();
                return 0;
            }

            if (!out_arg) {
                wchar_t *dot;
                wcsncpy(sz_out_buf, in_arg, MAX_PATH - 1);
                dot = wcsrchr(sz_out_buf, L'.');
                if (dot) *dot = L'\0';
                wcsncat(sz_out_buf, L"_formatted.pdf", MAX_PATH - wcslen(sz_out_buf) - 1);
                out_arg = sz_out_buf;
            }

            if (slides < 2 || slides > 4) slides = 3;

            {
                PDFEngineConfig cfg;
                char author_u8[256] = { 0 };
                char course_u8[256] = { 0 };
                char module_u8[256] = { 0 };
                char uni_u8[256] = { 0 };
                int out_pages = 0, out_slides = 0;
                char err[512] = { 0 };
                int res;

                pdf_engine_config_init(&cfg);
                cfg.slides_per_page = slides;
                cfg.draw_border = 1;
                cfg.draw_note_lines = 0;
                cfg.add_cover = 0;
                cfg.add_end = 0;

                if (author_arg) {
                    WideCharToMultiByte(CP_UTF8, 0, author_arg, -1, author_u8, sizeof(author_u8), NULL, NULL);
                    cfg.author_name = author_u8;
                }
                if (course_arg) {
                    WideCharToMultiByte(CP_UTF8, 0, course_arg, -1, course_u8, sizeof(course_u8), NULL, NULL);
                    cfg.course_name = course_u8;
                }
                if (module_arg) {
                    WideCharToMultiByte(CP_UTF8, 0, module_arg, -1, module_u8, sizeof(module_u8), NULL, NULL);
                    cfg.module_name = module_u8;
                }
                if (uni_arg) {
                    WideCharToMultiByte(CP_UTF8, 0, uni_arg, -1, uni_u8, sizeof(uni_u8), NULL, NULL);
                    cfg.university = uni_u8;
                }

                res = process_pdf_file_w(in_arg, out_arg, &cfg, NULL, NULL, &out_pages, &out_slides, err, sizeof(err));
                LocalFree(argv);
                CoUninitialize();
                return res;
            }
        }
    }

    if (argv) LocalFree(argv);

    {
        INITCOMMONCONTROLSEX icex;
        WNDCLASSEXW wc;
        HWND hwnd;
        MSG msg;
        RECT work;
        int x, y;

        icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
        icex.dwICC = ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&icex);

        g_dpi = query_system_dpi();
        gb_set_dpi(g_dpi);

        memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hInstance;
        wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
        wc.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(1));
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszClassName = L"SlideNoteFormatterGB";
        wc.style = CS_DROPSHADOW;
        if (!RegisterClassExW(&wc)) {
            CoUninitialize();
            return 1;
        }

        hwnd = CreateWindowExW(
            APP_WINDOW_EX_STYLE,
            L"SlideNoteFormatterGB",
            L"Slide Note Formatter",
            APP_WINDOW_STYLE,
            CW_USEDEFAULT, CW_USEDEFAULT,
            S(BASE_CLIENT_W), S(BASE_CLIENT_H),
            NULL, NULL, hInstance, NULL);

        if (!hwnd) {
            CoUninitialize();
            return 1;
        }

        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        x = work.left + (work.right - work.left - S(BASE_CLIENT_W)) / 2;
        y = work.top + (work.bottom - work.top - S(BASE_CLIENT_H)) / 2;
        SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

        ShowWindow(hwnd, nCmdShow);
        UpdateWindow(hwnd);

        while (GetMessageW(&msg, NULL, 0, 0) > 0) {
            if (!IsDialogMessageW(hwnd, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        CoUninitialize();
        return (int)msg.wParam;
    }
}
