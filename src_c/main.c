#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pdf_engine.h"

#define ID_BTN_BROWSE       101
#define ID_BTN_CONVERT      102
#define ID_BTN_OPEN_FOLDER  103
#define ID_EDT_INPUT        104
#define ID_CMB_SLIDES       105
#define ID_CHK_LINES        106
#define ID_CHK_BORDER       107
#define ID_CHK_COVER        108
#define ID_CHK_END          109
#define ID_EDT_COURSE       110
#define ID_EDT_MODULE       111
#define ID_EDT_AUTHOR       112
#define ID_EDT_UNI          113
#define ID_CHK_SUBFOLDER    114
#define ID_EDT_OUTDIR       115
#define ID_BTN_BROWSE_OUT   116

static HWND g_hwnd = NULL;
static HWND g_hInput = NULL;
static HWND g_hCmbSlides = NULL;
static HWND g_hChkLines = NULL;
static HWND g_hChkBorder = NULL;
static HWND g_hChkCover = NULL;
static HWND g_hChkEnd = NULL;
static HWND g_hEdtCourse = NULL;
static HWND g_hEdtModule = NULL;
static HWND g_hEdtAuthor = NULL;
static HWND g_hEdtUni = NULL;
static HWND g_hChkSubfolder = NULL;
static HWND g_hEdtOutDir = NULL;
static HWND g_hBtnBrowseOut = NULL;
static HWND g_hBtnConvert = NULL;
static HWND g_hBtnOpenFolder = NULL;
static HWND g_hStatus = NULL;

static HFONT g_hFont = NULL;
static HFONT g_hFontBold = NULL;
static HFONT g_hFontIcon = NULL;

static wchar_t **g_input_files = NULL;
static int g_input_count = 0;
static int g_input_capacity = 0;
static wchar_t g_last_output_dir[MAX_PATH] = { 0 };

static void clear_input_files() {
    for (int i = 0; i < g_input_count; i++) {
        if (g_input_files[i]) free(g_input_files[i]);
    }
    if (g_input_files) free(g_input_files);
    g_input_files = NULL;
    g_input_count = 0;
    g_input_capacity = 0;
}

static void add_input_file(const wchar_t *path) {
    if (!path || !*path) return;
    if (g_input_count >= g_input_capacity) {
        int ncap = g_input_capacity ? g_input_capacity * 2 : 16;
        wchar_t **p = (wchar_t **)realloc(g_input_files, ncap * sizeof(wchar_t *));
        if (!p) return;
        g_input_files = p;
        g_input_capacity = ncap;
    }
    g_input_files[g_input_count++] = _wcsdup(path);
}

static void set_status(const wchar_t *text) {
    if (g_hStatus) {
        SetWindowTextW(g_hStatus, text);
    }
}

static void update_input_display() {
    if (g_input_count == 0) {
        SetWindowTextW(g_hInput, L"");
        set_status(L"PDF dosyalari surukleyip birakin veya Gozat ile secin.");
        EnableWindow(g_hBtnConvert, FALSE);
    } else if (g_input_count == 1) {
        SetWindowTextW(g_hInput, g_input_files[0]);
        set_status(L"1 PDF secildi. Donusturmeye hazir.");
        EnableWindow(g_hBtnConvert, TRUE);
    } else {
        wchar_t buf[128];
        swprintf(buf, 128, L"%d PDF dosyasi secildi (Toplu Donusturme)", g_input_count);
        SetWindowTextW(g_hInput, buf);
        set_status(L"Toplu islem icin hazir. Donustur butonuna basin.");
        EnableWindow(g_hBtnConvert, TRUE);
    }
}

static void auto_detect_names(const wchar_t *filepath) {
    if (!filepath || !*filepath) return;
    const wchar_t *fname = wcsrchr(filepath, L'\\');
    if (!fname) fname = wcsrchr(filepath, L'/');
    if (fname) fname++; else fname = filepath;

    wchar_t name_no_ext[MAX_PATH];
    wcsncpy(name_no_ext, fname, MAX_PATH - 1);
    name_no_ext[MAX_PATH - 1] = L'\0';
    wchar_t *dot = wcsrchr(name_no_ext, L'.');
    if (dot) *dot = L'\0';

    SetWindowTextW(g_hEdtModule, name_no_ext);

    wchar_t code[64] = { 0 };
    int i = 0, j = 0;
    while (name_no_ext[i]) {
        if (iswalpha(name_no_ext[i])) {
            int start = i;
            while (iswalpha(name_no_ext[i])) i++;
            int alpha_len = i - start;
            int num_start = i;
            while (name_no_ext[i] == L' ') i++;
            while (iswdigit(name_no_ext[i])) i++;
            int digit_len = i - num_start;
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

static void on_browse() {
    wchar_t *szBuffer = (wchar_t *)calloc(65536, sizeof(wchar_t));
    if (!szBuffer) return;

    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"PDF Sunum Dosyalari (*.pdf)\0*.pdf\0Tum Dosyalar (*.*)\0*.*\0";
    ofn.lpstrFile = szBuffer;
    ofn.nMaxFile = 65535;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    ofn.lpstrTitle = L"Ders Slayti PDF Dosyalarini Secin (Coklu Secim Yapilabilir)";

    if (GetOpenFileNameW(&ofn)) {
        clear_input_files();

        wchar_t *p = szBuffer;
        wchar_t dir[MAX_PATH];
        wcsncpy(dir, p, MAX_PATH - 1);
        p += wcslen(p) + 1;

        if (*p == L'\0') {
            /* Single file */
            add_input_file(dir);
            auto_detect_names(dir);
        } else {
            /* Multiple files */
            while (*p) {
                wchar_t full_path[MAX_PATH];
                swprintf(full_path, MAX_PATH, L"%s\\%s", dir, p);
                add_input_file(full_path);
                p += wcslen(p) + 1;
            }
            if (g_input_count > 0) {
                auto_detect_names(g_input_files[0]);
            }
        }
        update_input_display();
    }
    free(szBuffer);
}

static void on_browse_output_dir() {
    BROWSEINFOW bi;
    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = g_hwnd;
    bi.lpszTitle = L"Cikti Klasorunu Secin:";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        wchar_t szPath[MAX_PATH] = { 0 };
        if (SHGetPathFromIDListW(pidl, szPath)) {
            SetWindowTextW(g_hEdtOutDir, szPath);
        }
        CoTaskMemFree(pidl);
    }
}

static void do_convert() {
    if (g_input_count == 0) {
        MessageBoxW(g_hwnd, L"Lutfen once en az bir PDF dosyasi secin veya surukleyip birakin.", L"Uyari", MB_ICONWARNING);
        return;
    }

    EnableWindow(g_hBtnConvert, FALSE);
    ShowWindow(g_hBtnOpenFolder, SW_HIDE);

    /* Output folder settings */
    int use_subfolder = (SendMessage(g_hChkSubfolder, BM_GETCHECK, 0, 0) == BST_CHECKED);
    wchar_t custom_out_dir[MAX_PATH] = { 0 };
    GetWindowTextW(g_hEdtOutDir, custom_out_dir, MAX_PATH);

    /* Configuration */
    PDFEngineConfig cfg;
    memset(&cfg, 0, sizeof(cfg));

    int sel = (int)SendMessage(g_hCmbSlides, CB_GETCURSEL, 0, 0);
    if (sel == 1) cfg.slides_per_page = 2;
    else if (sel == 2) cfg.slides_per_page = 4;
    else cfg.slides_per_page = 3;

    cfg.draw_note_lines = (SendMessage(g_hChkLines, BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg.draw_border = (SendMessage(g_hChkBorder, BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg.add_cover = (SendMessage(g_hChkCover, BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg.add_end = (SendMessage(g_hChkEnd, BM_GETCHECK, 0, 0) == BST_CHECKED);

    char course_u8[256] = { 0 };
    char module_u8[256] = { 0 };
    char author_u8[256] = { 0 };
    char uni_u8[256] = { 0 };

    wchar_t tmp_w[256];
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

    int total_converted = 0;
    int total_slides_all = 0;
    int total_pages_all = 0;

    for (int f_idx = 0; f_idx < g_input_count; f_idx++) {
        const wchar_t *in_path = g_input_files[f_idx];

        wchar_t status_msg[256];
        swprintf(status_msg, 256, L"[%d/%d] Donusturuluyor: %s", f_idx + 1, g_input_count, wcsrchr(in_path, L'\\') ? wcsrchr(in_path, L'\\') + 1 : in_path);
        set_status(status_msg);
        UpdateWindow(g_hwnd);

        /* Determine output directory */
        wchar_t out_dir[MAX_PATH] = { 0 };
        if (use_subfolder) {
            const wchar_t *slash = wcsrchr(in_path, L'\\');
            if (!slash) slash = wcsrchr(in_path, L'/');
            if (slash) {
                wcsncpy(out_dir, in_path, slash - in_path);
                wcsncat(out_dir, L"\\_notlar", MAX_PATH - wcslen(out_dir) - 1);
            } else {
                wcsncpy(out_dir, L"_notlar", MAX_PATH - 1);
            }
        } else if (custom_out_dir[0]) {
            wcsncpy(out_dir, custom_out_dir, MAX_PATH - 1);
        } else {
            const wchar_t *slash = wcsrchr(in_path, L'\\');
            if (!slash) slash = wcsrchr(in_path, L'/');
            if (slash) {
                wcsncpy(out_dir, in_path, slash - in_path);
            } else {
                GetCurrentDirectoryW(MAX_PATH, out_dir);
            }
        }

        CreateDirectoryW(out_dir, NULL);
        wcsncpy(g_last_output_dir, out_dir, MAX_PATH - 1);

        /* Construct output filename */
        const wchar_t *fname = wcsrchr(in_path, L'\\');
        if (!fname) fname = wcsrchr(in_path, L'/');
        if (fname) fname++; else fname = in_path;

        wchar_t fname_no_ext[MAX_PATH];
        wcsncpy(fname_no_ext, fname, MAX_PATH - 1);
        wchar_t *dot = wcsrchr(fname_no_ext, L'.');
        if (dot) *dot = L'\0';

        wchar_t out_file[MAX_PATH];
        swprintf(out_file, MAX_PATH, L"%s\\%s_notlar.pdf", out_dir, fname_no_ext);

        char in_u8[MAX_PATH * 3] = { 0 };
        char out_u8[MAX_PATH * 3] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, in_path, -1, in_u8, sizeof(in_u8), NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, out_file, -1, out_u8, sizeof(out_u8), NULL, NULL);

        int out_pages = 0, out_slides = 0;
        char err_msg[512] = { 0 };

        int res = process_pdf_file(in_u8, out_u8, &cfg, NULL, NULL, &out_pages, &out_slides, err_msg, sizeof(err_msg));
        if (res == 0) {
            total_converted++;
            total_slides_all += out_slides;
            total_pages_all += out_pages;
        } else {
            wchar_t err_w[512];
            MultiByteToWideChar(CP_UTF8, 0, err_msg, -1, err_w, 512);
            MessageBoxW(g_hwnd, err_w, L"Hata", MB_ICONERROR);
        }
    }

    EnableWindow(g_hBtnConvert, TRUE);

    if (total_converted > 0) {
        wchar_t done_msg[256];
        swprintf(done_msg, 256, L"Tamamlandi! %d dosya (%d slayt -> %d sayfa) hazirlandi.", total_converted, total_slides_all, total_pages_all);
        set_status(done_msg);

        ShowWindow(g_hBtnOpenFolder, SW_SHOW);
        EnableWindow(g_hBtnOpenFolder, TRUE);

        wchar_t success_dialog[512];
        swprintf(success_dialog, 512,
            L"Donusturme Basariyla Tamamlandi!\n\n"
            L"Donusturulen Dosya: %d\n"
            L"Toplam Slayt: %d\n"
            L"Cikti Sayfasi: %d\n\n"
            L"Klasor: %s\n\n"
            L"Klasoru acmak icin sagdaki kucuk klasor butonuna tiklayabilirsiniz.",
            total_converted, total_slides_all, total_pages_all, g_last_output_dir);
        MessageBoxW(g_hwnd, success_dialog, L"Islem Basarili", MB_ICONINFORMATION);
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hwnd = hwnd;
            DragAcceptFiles(hwnd, TRUE);

            g_hFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontBold = CreateFontW(-14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            g_hFontIcon = CreateFontW(-20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Emoji");

            /* Header Label */
            HWND hHdr = CreateWindowW(L"STATIC", L"Ders sunum PDF dosyasini (veya birden fazla dosyayi) secin:",
                                      WS_CHILD | WS_VISIBLE, 20, 12, 520, 20, hwnd, NULL, NULL, NULL);
            SendMessage(hHdr, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

            /* Input File Box */
            g_hInput = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                       WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                                       20, 34, 430, 26, hwnd, (HMENU)ID_EDT_INPUT, NULL, NULL);
            SendMessage(g_hInput, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            HWND hBtnBrowse = CreateWindowW(L"BUTTON", L"Gözat...",
                                            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                            460, 33, 80, 28, hwnd, (HMENU)ID_BTN_BROWSE, NULL, NULL);
            SendMessage(hBtnBrowse, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            /* Group 1: Output Location */
            HWND hGrpOut = CreateWindowW(L"BUTTON", L"Cikti Klasoru Ayarlari",
                                         WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                         20, 72, 520, 92, hwnd, NULL, NULL, NULL);
            SendMessage(hGrpOut, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

            g_hChkSubfolder = CreateWindowW(L"BUTTON", L"Ciktilari ayni dizindeki alt klasore kaydet (_notlar)",
                                           WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                           35, 95, 480, 22, hwnd, (HMENU)ID_CHK_SUBFOLDER, NULL, NULL);
            SendMessage(g_hChkSubfolder, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            SendMessage(g_hChkSubfolder, BM_SETCHECK, BST_CHECKED, 0);

            HWND hLblCustomOut = CreateWindowW(L"STATIC", L"Ozel Cikti Klasoru:",
                                              WS_CHILD | WS_VISIBLE, 35, 128, 120, 20, hwnd, NULL, NULL, NULL);
            SendMessage(hLblCustomOut, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            g_hEdtOutDir = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                           WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_DISABLED,
                                           160, 125, 320, 24, hwnd, (HMENU)ID_EDT_OUTDIR, NULL, NULL);
            SendMessage(g_hEdtOutDir, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            g_hBtnBrowseOut = CreateWindowW(L"BUTTON", L"...",
                                            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
                                            490, 124, 40, 26, hwnd, (HMENU)ID_BTN_BROWSE_OUT, NULL, NULL);
            SendMessage(g_hBtnBrowseOut, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            /* Group 2: Format Options */
            HWND hGrp1 = CreateWindowW(L"BUTTON", L"Sayfa Duzen Ayarlari",
                                       WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                       20, 175, 520, 90, hwnd, NULL, NULL, NULL);
            SendMessage(hGrp1, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

            HWND hLblSlides = CreateWindowW(L"STATIC", L"Sayfa Basina Slayt:",
                                           WS_CHILD | WS_VISIBLE, 35, 198, 130, 20, hwnd, NULL, NULL, NULL);
            SendMessage(hLblSlides, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            g_hCmbSlides = CreateWindowW(L"COMBOBOX", L"",
                                         WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                         170, 195, 190, 120, hwnd, (HMENU)ID_CMB_SLIDES, NULL, NULL);
            SendMessage(g_hCmbSlides, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            SendMessage(g_hCmbSlides, CB_ADDSTRING, 0, (LPARAM)L"3 Slayt (Onerilen)");
            SendMessage(g_hCmbSlides, CB_ADDSTRING, 0, (LPARAM)L"2 Slayt (Genis Not)");
            SendMessage(g_hCmbSlides, CB_ADDSTRING, 0, (LPARAM)L"4 Slayt (Kompakt)");
            SendMessage(g_hCmbSlides, CB_SETCURSEL, 0, 0);

            g_hChkLines = CreateWindowW(L"BUTTON", L"Sag tarafa cizgili not alani ekle",
                                        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                        35, 230, 230, 22, hwnd, (HMENU)ID_CHK_LINES, NULL, NULL);
            SendMessage(g_hChkLines, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            SendMessage(g_hChkLines, BM_SETCHECK, BST_CHECKED, 0);

            g_hChkBorder = CreateWindowW(L"BUTTON", L"Slayt cerceve kenarligi",
                                         WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                         285, 230, 200, 22, hwnd, (HMENU)ID_CHK_BORDER, NULL, NULL);
            SendMessage(g_hChkBorder, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            SendMessage(g_hChkBorder, BM_SETCHECK, BST_CHECKED, 0);

            /* Group 3: Metadata & Cover */
            HWND hGrp2 = CreateWindowW(L"BUTTON", L"Kapak & Dokuman Bilgileri (Istege Bagli)",
                                       WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                       20, 275, 520, 135, hwnd, NULL, NULL, NULL);
            SendMessage(hGrp2, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

            g_hChkCover = CreateWindowW(L"BUTTON", L"On kapak sayfasi ekle",
                                        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                        35, 298, 180, 22, hwnd, (HMENU)ID_CHK_COVER, NULL, NULL);
            SendMessage(g_hChkCover, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            SendMessage(g_hChkCover, BM_SETCHECK, BST_CHECKED, 0);

            g_hChkEnd = CreateWindowW(L"BUTTON", L"Bitis sayfasi ekle",
                                      WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                      285, 298, 180, 22, hwnd, (HMENU)ID_CHK_END, NULL, NULL);
            SendMessage(g_hChkEnd, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            SendMessage(g_hChkEnd, BM_SETCHECK, BST_CHECKED, 0);

            HWND hLblC = CreateWindowW(L"STATIC", L"Ders Adi / Kodu:", WS_CHILD | WS_VISIBLE, 35, 332, 110, 18, hwnd, NULL, NULL, NULL);
            SendMessage(hLblC, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            g_hEdtCourse = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 150, 329, 150, 24, hwnd, (HMENU)ID_EDT_COURSE, NULL, NULL);
            SendMessage(g_hEdtCourse, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            HWND hLblM = CreateWindowW(L"STATIC", L"Konu / Modul:", WS_CHILD | WS_VISIBLE, 315, 332, 90, 18, hwnd, NULL, NULL, NULL);
            SendMessage(hLblM, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            g_hEdtModule = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 410, 329, 115, 24, hwnd, (HMENU)ID_EDT_MODULE, NULL, NULL);
            SendMessage(g_hEdtModule, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            HWND hLblA = CreateWindowW(L"STATIC", L"Ogrenci / Not Alan:", WS_CHILD | WS_VISIBLE, 35, 367, 110, 18, hwnd, NULL, NULL, NULL);
            SendMessage(hLblA, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            g_hEdtAuthor = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 150, 364, 150, 24, hwnd, (HMENU)ID_EDT_AUTHOR, NULL, NULL);
            SendMessage(g_hEdtAuthor, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            HWND hLblU = CreateWindowW(L"STATIC", L"Universite / Okul:", WS_CHILD | WS_VISIBLE, 315, 367, 90, 18, hwnd, NULL, NULL, NULL);
            SendMessage(hLblU, WM_SETFONT, (WPARAM)g_hFont, TRUE);
            g_hEdtUni = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 410, 364, 115, 24, hwnd, (HMENU)ID_EDT_UNI, NULL, NULL);
            SendMessage(g_hEdtUni, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            /* Action Buttons Row */
            g_hBtnConvert = CreateWindowW(L"BUTTON", L"PDF'leri Not Formatina Donustur",
                                          WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                          20, 424, 455, 42, hwnd, (HMENU)ID_BTN_CONVERT, NULL, NULL);
            SendMessage(g_hBtnConvert, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);

            /* Intuitive Small Folder Button (no text) */
            g_hBtnOpenFolder = CreateWindowW(L"BUTTON", L"\x25A6", /* Will use icon or folder symbol */
                                             WS_CHILD | BS_PUSHBUTTON,
                                             485, 424, 55, 42, hwnd, (HMENU)ID_BTN_OPEN_FOLDER, NULL, NULL);
            SetWindowTextW(g_hBtnOpenFolder, L"\xD83D\xDCC1"); /* 📁 Folder emoji in UTF-16 surrogate pair */
            SendMessage(g_hBtnOpenFolder, WM_SETFONT, (WPARAM)g_hFontIcon, TRUE);

            /* Status Label */
            g_hStatus = CreateWindowW(L"STATIC", L"PDF dosyalari surukleyip birakin veya Gozat ile secin.",
                                      WS_CHILD | WS_VISIBLE, 20, 478, 520, 22, hwnd, NULL, NULL, NULL);
            SendMessage(g_hStatus, WM_SETFONT, (WPARAM)g_hFont, TRUE);

            break;
        }

        case WM_DROPFILES: {
            HDROP hDrop = (HDROP)wParam;
            UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, NULL, 0);
            if (count > 0) {
                clear_input_files();
                for (UINT i = 0; i < count; i++) {
                    wchar_t szFile[MAX_PATH];
                    if (DragQueryFileW(hDrop, i, szFile, MAX_PATH)) {
                        const wchar_t *ext = wcsrchr(szFile, L'.');
                        if (ext && _wcsicmp(ext, L".pdf") == 0) {
                            add_input_file(szFile);
                        }
                    }
                }
                if (g_input_count > 0) {
                    auto_detect_names(g_input_files[0]);
                }
                update_input_display();
            }
            DragFinish(hDrop);
            break;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (wmId == ID_BTN_BROWSE) {
                on_browse();
            } else if (wmId == ID_CHK_SUBFOLDER) {
                int chk = (SendMessage(g_hChkSubfolder, BM_GETCHECK, 0, 0) == BST_CHECKED);
                EnableWindow(g_hEdtOutDir, !chk);
                EnableWindow(g_hBtnBrowseOut, !chk);
            } else if (wmId == ID_BTN_BROWSE_OUT) {
                on_browse_output_dir();
            } else if (wmId == ID_BTN_CONVERT) {
                do_convert();
            } else if (wmId == ID_BTN_OPEN_FOLDER) {
                if (g_last_output_dir[0]) {
                    ShellExecuteW(hwnd, L"open", g_last_output_dir, NULL, NULL, SW_SHOWNORMAL);
                }
            }
            break;
        }

        case WM_DESTROY: {
            clear_input_files();
            if (g_hFont) DeleteObject(g_hFont);
            if (g_hFontBold) DeleteObject(g_hFontBold);
            if (g_hFontIcon) DeleteObject(g_hFontIcon);
            PostQuitMessage(0);
            break;
        }

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    /* CLI Mode: if arguments are passed */
    if (argc > 1) {
        if (wcscmp(argv[1], L"-h") == 0 || wcscmp(argv[1], L"--help") == 0 || wcscmp(argv[1], L"/?") == 0) {
            MessageBoxW(NULL,
                L"Slide Note Formatter (Ultra-Lightweight <1MB)\n\n"
                L"Kullanim (CLI):\n"
                L"  SlideNoteFormatter.exe <input.pdf> [output.pdf] [slides_per_page] [-author \"Ad Soyad\"]\n\n"
                L"Ornek:\n"
                L"  SlideNoteFormatter.exe ders.pdf ders_not.pdf 3 -author \"Haluk Tanriseven\"\n\n"
                L"Veya dogrudan PDF dosyalarini programin uzerine surukleyip birakin.",
                L"Kullanim Yardimi", MB_ICONINFORMATION);
            LocalFree(argv);
            return 0;
        }

        wchar_t *in_arg = NULL;
        wchar_t sz_out_buf[MAX_PATH] = { 0 };
        wchar_t *out_arg = NULL;
        int slides = 3;
        wchar_t *author_arg = NULL;
        wchar_t *course_arg = NULL;
        wchar_t *module_arg = NULL;
        wchar_t *uni_arg = NULL;

        for (int i = 1; i < argc; i++) {
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
            return 0;
        }

        if (!out_arg) {
            wcsncpy(sz_out_buf, in_arg, MAX_PATH - 1);
            wchar_t *dot = wcsrchr(sz_out_buf, L'.');
            if (dot) *dot = L'\0';
            wcsncat(sz_out_buf, L"_notlar.pdf", MAX_PATH - wcslen(sz_out_buf) - 1);
            out_arg = sz_out_buf;
        }

        if (slides < 2 || slides > 4) slides = 3;

        PDFEngineConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.slides_per_page = slides;
        cfg.draw_border = 1;
        cfg.draw_note_lines = 1;
        cfg.add_cover = 1;
        cfg.add_end = 1;

        char in_u8[MAX_PATH * 3] = { 0 };
        char out_u8[MAX_PATH * 3] = { 0 };
        char author_u8[256] = { 0 };
        char course_u8[256] = { 0 };
        char module_u8[256] = { 0 };
        char uni_u8[256] = { 0 };

        WideCharToMultiByte(CP_UTF8, 0, in_arg, -1, in_u8, sizeof(in_u8), NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, out_arg, -1, out_u8, sizeof(out_u8), NULL, NULL);

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

        int out_pages = 0, out_slides = 0;
        char err[512] = { 0 };
        int res = process_pdf_file(in_u8, out_u8, &cfg, NULL, NULL, &out_pages, &out_slides, err, sizeof(err));

        LocalFree(argv);
        return res;
    }

    if (argv) LocalFree(argv);

    /* GUI Mode */
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(1));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"SlideNoteFormatterApp";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        L"SlideNoteFormatterApp",
        L"Slayt Not Formatlayici (Ultra-Lightweight)",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 575, 545,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
