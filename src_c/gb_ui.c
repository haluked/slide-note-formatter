#include "gb_ui.h"

#include <windows.h>
#include <wchar.h>
#include <string.h>

#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

static UINT g_gb_dpi = 96;

/* Corner radius in design units (96 DPI base). */
#define GB_RADIUS_SMALL 3
#define GB_RADIUS_LARGE 5

int gb_scale(int value)
{
    return MulDiv(value, (int)g_gb_dpi, 96);
}

void gb_set_dpi(UINT dpi)
{
    if (dpi == 0) dpi = 96;
    g_gb_dpi = dpi;
}

void gb_fill(HDC hdc, const RECT *rc, COLORREF color)
{
    HBRUSH br;
    if (!rc) return;
    br = CreateSolidBrush(color);
    if (!br) return;
    FillRect(hdc, rc, br);
    DeleteObject(br);
}

void gb_frame(HDC hdc, const RECT *rc, COLORREF color, int thickness)
{
    RECT r;
    HBRUSH br;
    int t;

    if (!rc || thickness <= 0) return;
    t = thickness;
    br = CreateSolidBrush(color);
    if (!br) return;

    r.left = rc->left;
    r.top = rc->top;
    r.right = rc->right;
    r.bottom = rc->top + t;
    FillRect(hdc, &r, br);

    r.top = rc->bottom - t;
    r.bottom = rc->bottom;
    FillRect(hdc, &r, br);

    r.left = rc->left;
    r.top = rc->top;
    r.right = rc->left + t;
    r.bottom = rc->bottom;
    FillRect(hdc, &r, br);

    r.left = rc->right - t;
    r.right = rc->right;
    FillRect(hdc, &r, br);

    DeleteObject(br);
}

/* -------------------------------------------------------------- corners */

int gb_radius(int large)
{
    int r = gb_scale(large ? GB_RADIUS_LARGE : GB_RADIUS_SMALL);
    if (r < 0) r = 0;
    return r;
}

/* Paints the pixels outside the 45-degree cut with `bg`; the rest is left
   untouched. mask: 1=top left, 2=top right, 4=bottom left, 8=bottom right. */
static void gb_cut_corners(HDC hdc, const RECT *rc, int r,
                           COLORREF bg, unsigned mask)
{
    int c, i, j;
    int w, h;

    if (!rc || r <= 0 || mask == 0) return;
    w = rc->right - rc->left;
    h = rc->bottom - rc->top;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r <= 0) return;

    for (c = 0; c < 4; c++) {
        int ox, oy, sx, sy;
        if (!(mask & (1u << c))) continue;

        ox = (c & 1) ? rc->right - 1 : rc->left;
        oy = (c & 2) ? rc->bottom - 1 : rc->top;
        sx = (c & 1) ? -1 : 1;
        sy = (c & 2) ? -1 : 1;

        for (j = 0; j < r; j++) {
            for (i = 0; i < r; i++) {
                if (i + j >= r) continue;   /* inside the 45-degree cut */
                SetPixelV(hdc, ox + sx * i, oy + sy * j, bg);
            }
        }
    }
}

void gb_fill_round(HDC hdc, const RECT *rc, COLORREF color, COLORREF bg)
{
    if (!rc) return;
    gb_fill(hdc, rc, color);
    gb_cut_corners(hdc, rc, gb_radius(0), bg, 15u);
}

void gb_frame_round(HDC hdc, const RECT *rc, COLORREF color, int thickness,
                    COLORREF bg, COLORREF inner, int large)
{
    RECT in;
    int r;

    if (!rc || thickness <= 0) return;
    r = gb_radius(large);

    gb_fill(hdc, rc, color);
    gb_cut_corners(hdc, rc, r, bg, 15u);

    in = *rc;
    InflateRect(&in, -thickness, -thickness);
    if (in.right <= in.left || in.bottom <= in.top) return;

    gb_fill(hdc, &in, inner);
    gb_cut_corners(hdc, &in, r - thickness > 0 ? r - thickness : 0, color, 15u);
}

static HRGN gb_shape_rgn(const RECT *rc, int r)
{
    HRGN rgn;
    int w, h;

    if (!rc) return NULL;
    w = rc->right - rc->left;
    h = rc->bottom - rc->top;
    if (w <= 0 || h <= 0) return NULL;

    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r <= 0) return CreateRectRgn(rc->left, rc->top, rc->right, rc->bottom);

    {
        POINT p[8];
        p[0].x = rc->left + r;  p[0].y = rc->top;
        p[1].x = rc->right - r; p[1].y = rc->top;
        p[2].x = rc->right;     p[2].y = rc->top + r;
        p[3].x = rc->right;     p[3].y = rc->bottom - r;
        p[4].x = rc->right - r; p[4].y = rc->bottom;
        p[5].x = rc->left + r;  p[5].y = rc->bottom;
        p[6].x = rc->left;      p[6].y = rc->bottom - r;
        p[7].x = rc->left;      p[7].y = rc->top + r;
        rgn = CreatePolygonRgn(p, 8, ALTERNATE);
    }
    return rgn;
}

void gb_shadow(HDC hdc, const RECT *body, int offset, COLORREF color)
{
    RECT s;
    HRGN sh, bo;
    int r, saved;

    if (!hdc || !body || offset <= 0) return;
    r = gb_radius(0);

    s = *body;
    OffsetRect(&s, offset, offset);

    sh = gb_shape_rgn(&s, r);
    bo = gb_shape_rgn(body, r);
    if (!sh || !bo) {
        if (sh) DeleteObject(sh);
        if (bo) DeleteObject(bo);
        return;
    }

    CombineRgn(sh, sh, bo, RGN_DIFF);
    saved = SaveDC(hdc);
    if (saved) {
        SelectClipRgn(hdc, sh);
        gb_fill(hdc, &s, color);
        RestoreDC(hdc, saved);
    }
    DeleteObject(sh);
    DeleteObject(bo);

    /* Close the 1 px gap between the region scan and the pixel clip:
       the cut pixels at the bottom-right corner connect to the shadow. */
    gb_cut_corners(hdc, body, r, color, 8u);
}

void gb_round_window(HWND hWnd, int large)
{
    RECT rc;
    HRGN rgn;
    int w, h, r;

    if (!hWnd || !IsWindow(hWnd)) return;
    GetWindowRect(hWnd, &rc);
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    r = gb_radius(large);
    if (w <= 0 || h <= 0) return;

    if (r <= 0) {
        SetWindowRgn(hWnd, NULL, TRUE);
        return;
    }
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    {
        POINT pts[8];
        pts[0].x = r;     pts[0].y = 0;
        pts[1].x = w - r; pts[1].y = 0;
        pts[2].x = w;     pts[2].y = r;
        pts[3].x = w;     pts[3].y = h - r;
        pts[4].x = w - r; pts[4].y = h;
        pts[5].x = r;     pts[5].y = h;
        pts[6].x = 0;     pts[6].y = h - r;
        pts[7].x = 0;     pts[7].y = r;
        rgn = CreatePolygonRgn(pts, 8, ALTERNATE);
    }
    /* On success, ownership of the region passes to the system. */
    SetWindowRgn(hWnd, rgn, TRUE);
}

void gb_dither(HDC hdc, const RECT *rc, COLORREF color)
{
    int x, y;
    if (!rc) return;
    for (y = rc->top; y < rc->bottom; y += 2) {
        for (x = rc->left + ((y / 2) & 1); x < rc->right; x += 2) {
            SetPixelV(hdc, x, y, color);
        }
    }
}

void gb_text(HDC hdc, HFONT font, COLORREF color, const RECT *rc,
             const wchar_t *text, UINT flags)
{
    HGDIOBJ old;
    int old_mode;

    if (!text) text = L"";
    old_mode = SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    old = SelectObject(hdc, font);
    DrawTextW(hdc, text, -1, (LPRECT)rc, flags);
    SelectObject(hdc, old);
    SetBkMode(hdc, old_mode);
}

void gb_icon(HDC hdc, int x, int y, int scale, int kind)
{
    int s = scale;
    RECT r;

    if (s < 1) s = 1;

    if (kind == 0) {
        /* Small screen: light frame + dark glass + light play triangle.
           The tones are inverted because the title bar sits on a dark background. */
        r.left = x; r.top = y; r.right = x + 12 * s; r.bottom = y + 10 * s;
        gb_fill(hdc, &r, GB_LIGHTEST);
        r.left = x + 2 * s; r.top = y + 2 * s;
        r.right = x + 10 * s; r.bottom = y + 8 * s;
        gb_fill(hdc, &r, GB_DARKEST);

        r.left = x + 4 * s; r.top = y + 3 * s;
        r.right = x + 5 * s; r.bottom = y + 7 * s;
        gb_fill(hdc, &r, GB_LIGHTEST);
        r.left = x + 5 * s; r.top = y + 4 * s;
        r.right = x + 6 * s; r.bottom = y + 6 * s;
        gb_fill(hdc, &r, GB_LIGHTEST);
        r.left = x + 6 * s; r.top = y + 5 * s;
        r.right = x + 7 * s; r.bottom = y + 6 * s;
        gb_fill(hdc, &r, GB_LIGHTEST);
    } else {
        /* Folder icon */
        r.left = x; r.top = y + s; r.right = x + 4 * s; r.bottom = y + 3 * s;
        gb_fill(hdc, &r, GB_DARKEST);
        r.left = x; r.top = y + 2 * s; r.right = x + 10 * s; r.bottom = y + 9 * s;
        gb_fill(hdc, &r, GB_DARKEST);
        r.left = x + s; r.top = y + 3 * s; r.right = x + 9 * s; r.bottom = y + 8 * s;
        gb_fill(hdc, &r, GB_LIGHTEST);
        r.left = x + s; r.top = y + 4 * s; r.right = x + 9 * s; r.bottom = y + 5 * s;
        gb_fill(hdc, &r, GB_DARK);
    }
}

/* ---------------------------------------------------------------- denetim */

static int gb_is_disabled(const DRAWITEMSTRUCT *dis)
{
    return (dis->itemState & ODS_DISABLED) != 0;
}

static void gb_text_in(const RECT *box, HDC hdc, HFONT font, COLORREF color,
                       const wchar_t *text, UINT flags)
{
    RECT r = *box;
    gb_text(hdc, font, color, &r, text, flags);
}

void gb_draw_control(const DRAWITEMSTRUCT *dis, HFONT font, int role,
                     int checked, int hover)
{
    RECT rc, body, text_rc, box;
    wchar_t label[512];
    int w, h, pad, border, shadow, pressed, disabled;
    COLORREF face, ink, line;

    if (!dis) return;
    rc = dis->rcItem;
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    label[0] = L'\0';
    GetWindowTextW(dis->hwndItem, label, 511);

    border = gb_scale(2);
    if (border < 2) border = 2;
    pad = gb_scale(6);
    if (pad < 2) pad = 2;
    shadow = gb_scale(2);
    if (shadow < 1) shadow = 1;

    pressed = (dis->itemState & ODS_SELECTED) != 0;
    disabled = gb_is_disabled(dis);

    /* Background: all controls sit on a panel (light surface). */
    gb_fill(dis->hDC, &rc, GB_LIGHTEST);

    if (role == GB_ROLE_CHECK) {
        box.left = rc.left + pad;
        box.top = rc.top + (h - gb_scale(14)) / 2;
        box.right = box.left + gb_scale(14);
        box.bottom = box.top + gb_scale(14);

        gb_fill(dis->hDC, &box, GB_LIGHTEST);
        gb_frame(dis->hDC, &box, disabled ? GB_DARK : GB_DARKEST, border);
        if (checked) {
            RECT in = box;
            in.left += border + 1;
            in.top += border + 1;
            in.right -= border + 1;
            in.bottom -= border + 1;
            gb_fill(dis->hDC, &in, disabled ? GB_LIGHT : GB_DARKEST);

            /* Piksel onay isareti */
            {
                int s = gb_scale(2);
                int bx = in.left + (in.right - in.left) / 2;
                int by = in.top + (in.bottom - in.top) / 2;
                RECT p;
                p.left = bx - 3 * s; p.top = by; p.right = bx - 1 * s; p.bottom = by + s;
                gb_fill(dis->hDC, &p, GB_LIGHTEST);
                p.left = bx - s; p.top = by + s; p.right = bx + s; p.bottom = by + 2 * s;
                gb_fill(dis->hDC, &p, GB_LIGHTEST);
                p.left = bx + s; p.top = by - s; p.right = bx + 3 * s; p.bottom = by + s;
                gb_fill(dis->hDC, &p, GB_LIGHTEST);
            }
        }

        text_rc.left = box.right + gb_scale(6);
        text_rc.top = rc.top;
        text_rc.right = rc.right - pad;
        text_rc.bottom = rc.bottom;
        gb_text_in(&text_rc, dis->hDC, font, disabled ? GB_DARK : GB_DARKEST,
                   label, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        return;
    }

    if (role == GB_ROLE_CHIP) {
        body = rc;
        if (checked) {
            gb_frame_round(dis->hDC, &body, GB_DARKEST, border, GB_LIGHTEST,
                           GB_DARKEST, 0);
            ink = GB_LIGHTEST;
        } else {
            gb_frame_round(dis->hDC, &body, disabled ? GB_DARK : GB_DARK,
                           border, GB_LIGHTEST, hover ? GB_LIGHT : GB_LIGHTEST, 0);
            ink = disabled ? GB_DARK : GB_DARKEST;
        }
        text_rc = body;
        text_rc.left += pad;
        text_rc.right -= pad;
        if (pressed) OffsetRect(&text_rc, 0, gb_scale(1));
        gb_text_in(&text_rc, dis->hDC, font, ink, label,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (dis->itemState & ODS_FOCUS) {
            RECT f = rc;
            InflateRect(&f, -border, -border);
            gb_frame(dis->hDC, &f, GB_DARKEST, 1);
        }
        return;
    }

    if (role == GB_ROLE_TITLE) {
        int cx, cy, i;
        body = rc;
        body.right -= shadow;
        body.bottom -= shadow;
        gb_fill_round(dis->hDC, &body, hover ? GB_DARKEST : GB_DARK, GB_DARK);
        gb_frame_round(dis->hDC, &body, GB_DARKEST, border, GB_DARK,
                       hover ? GB_DARKEST : GB_DARK, 0);

        cx = (body.left + body.right) / 2;
        cy = (body.top + body.bottom) / 2;
        if (pressed) cy += gb_scale(1);

        if (checked) {
            /* Close: 2 px thick X */
            for (i = -3; i <= 3; i++) {
                RECT p;
                p.left = cx + gb_scale(i);
                p.top = cy + gb_scale(i);
                p.right = p.left + gb_scale(2);
                p.bottom = p.top + gb_scale(2);
                gb_fill(dis->hDC, &p, GB_LIGHTEST);
                p.left = cx + gb_scale(i);
                p.top = cy - gb_scale(i);
                p.right = p.left + gb_scale(2);
                p.bottom = p.top + gb_scale(2);
                gb_fill(dis->hDC, &p, GB_LIGHTEST);
            }
        } else {
            /* Minimize: horizontal line */
            RECT m;
            m.left = cx - gb_scale(4);
            m.top = cy - gb_scale(1);
            m.right = cx + gb_scale(4);
            m.bottom = cy + gb_scale(1);
            gb_fill(dis->hDC, &m, GB_LIGHTEST);
        }
        return;
    }

    /* PUSH / PRIMARY / DANGER / DROP ortak govde */
    face = (role == GB_ROLE_PRIMARY || role == GB_ROLE_DANGER) ? GB_DARKEST : GB_LIGHTEST;
    ink = (role == GB_ROLE_PRIMARY || role == GB_ROLE_DANGER) ? GB_LIGHTEST : GB_DARKEST;
    line = GB_DARKEST;

    if (disabled) {
        gb_fill(dis->hDC, &rc, GB_LIGHTEST);
        gb_dither(dis->hDC, &rc, GB_LIGHT);
        face = GB_LIGHTEST;
        ink = GB_DARK;
        line = GB_DARK;
    } else if (pressed) {
        face = GB_DARK;
        ink = GB_LIGHTEST;
    } else if (hover) {
        face = (role == GB_ROLE_PRIMARY || role == GB_ROLE_DANGER) ? GB_DARK : GB_DARK;
        ink = GB_LIGHTEST;
    }

    body = rc;
    body.right -= shadow;
    body.bottom -= shadow;

    gb_fill_round(dis->hDC, &body, face, GB_LIGHTEST);
    gb_frame_round(dis->hDC, &body, line, border, GB_LIGHTEST, face, 0);

    /* Bottom/right shadow: disappears when pressed */
    if (!pressed && !disabled) {
        gb_shadow(dis->hDC, &body, shadow, GB_DARK);
    }

    text_rc = body;
    text_rc.left += pad;
    text_rc.right -= pad;
    if (pressed) OffsetRect(&text_rc, 0, gb_scale(1));

    if (role == GB_ROLE_DROP) {
        RECT arrow;
        int ax, ay;
        text_rc.right -= gb_scale(16);
        gb_text_in(&text_rc, dis->hDC, font, ink, label,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        ax = body.right - gb_scale(14);
        ay = body.top + (body.bottom - body.top) / 2 - gb_scale(1);
        arrow.left = ax; arrow.top = ay; arrow.right = ax + gb_scale(8); arrow.bottom = ay + gb_scale(2);
        gb_fill(dis->hDC, &arrow, ink);
        arrow.left = ax + gb_scale(2); arrow.top = ay + gb_scale(2);
        arrow.right = ax + gb_scale(6); arrow.bottom = ay + gb_scale(4);
        gb_fill(dis->hDC, &arrow, ink);
        arrow.left = ax + gb_scale(4); arrow.top = ay + gb_scale(4);
        arrow.right = ax + gb_scale(4) + 1; arrow.bottom = ay + gb_scale(6);
        gb_fill(dis->hDC, &arrow, ink);
    } else {
        gb_text_in(&text_rc, dis->hDC, font, ink, label,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    if ((dis->itemState & ODS_FOCUS) && !disabled) {
        RECT f = body;
        InflateRect(&f, -border - 1, -border - 1);
        gb_frame(dis->hDC, &f, ink, 1);
    }
}

/* ---------------------------------------------------------------- popup */

#define GB_DROP_CLASS L"GbDropPopupWnd"
#define GB_DROP_MAX_ITEMS 32
#define GB_DROP_ITEM_CHARS 128

typedef struct {
    HWND hwnd;
    HWND owner;
    /* Own copy of the rows. The caller's array/strings may live on its stack and
       become invalid as soon as gb_dropdown_open() returns, while the popup is
       still open and repainting. */
    wchar_t items[GB_DROP_MAX_ITEMS][GB_DROP_ITEM_CHARS];
    int count;
    int selected;
    int hover;
    GbPickCallback cb;
    void *user;
    HFONT font;
    int row_h;
    int pad;
} GbDropState;

static GbDropState g_drop;

static void gb_drop_paint(HWND hWnd)
{
    PAINTSTRUCT ps;
    HDC hdc;
    RECT rc;
    int i;
    int y;

    hdc = BeginPaint(hWnd, &ps);
    GetClientRect(hWnd, &rc);
    gb_fill(hdc, &rc, GB_LIGHTEST);
    gb_frame(hdc, &rc, GB_DARKEST, gb_scale(2));

    y = g_drop.pad;
    for (i = 0; i < g_drop.count; i++) {
        RECT row;
        RECT text_rc;
        COLORREF ink;

        row.left = gb_scale(2);
        row.right = rc.right - gb_scale(2);
        row.top = y;
        row.bottom = y + g_drop.row_h;

        if (i == g_drop.selected) {
            gb_fill(hdc, &row, GB_DARKEST);
            ink = GB_LIGHTEST;
        } else if (i == g_drop.hover) {
            gb_fill(hdc, &row, GB_DARK);
            ink = GB_LIGHTEST;
        } else {
            ink = GB_DARKEST;
        }

        text_rc = row;
        text_rc.left += gb_scale(8);
        text_rc.right -= gb_scale(6);
        if (i == g_drop.selected) {
            RECT mark;
            mark.left = row.left + gb_scale(3);
            mark.top = row.top + g_drop.row_h / 2 - gb_scale(2);
            mark.right = mark.left + gb_scale(2);
            mark.bottom = mark.top + gb_scale(4);
            gb_fill(hdc, &mark, ink);
            text_rc.left += gb_scale(6);
        }
        gb_text(hdc, g_drop.font, ink, &text_rc, g_drop.items[i],
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        y += g_drop.row_h;
    }

    EndPaint(hWnd, &ps);
}

static int gb_drop_row_at(int y)
{
    int idx;
    if (y < g_drop.pad) return -1;
    idx = (y - g_drop.pad) / g_drop.row_h;
    if (idx < 0 || idx >= g_drop.count) return -1;
    return idx;
}

static LRESULT CALLBACK GbDropProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_PAINT:
            gb_drop_paint(hWnd);
            return 0;

        case WM_MOUSEMOVE: {
            int idx = gb_drop_row_at(GET_Y_LPARAM(lParam));
            if (idx != g_drop.hover) {
                g_drop.hover = idx;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            int idx = gb_drop_row_at(GET_Y_LPARAM(lParam));
            if (idx >= 0) {
                g_drop.selected = idx;
                if (g_drop.cb) g_drop.cb(idx, g_drop.user);
            }
            DestroyWindow(hWnd);
            return 0;
        }

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                DestroyWindow(hWnd);
                return 0;
            } else if (wParam == VK_UP) {
                if (g_drop.selected > 0) g_drop.selected--;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            } else if (wParam == VK_DOWN) {
                if (g_drop.selected + 1 < g_drop.count) g_drop.selected++;
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            } else if (wParam == VK_RETURN) {
                if (g_drop.cb) g_drop.cb(g_drop.selected, g_drop.user);
                DestroyWindow(hWnd);
                return 0;
            }
            break;

        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                DestroyWindow(hWnd);
                return 0;
            }
            break;

        case WM_MOUSEACTIVATE:
            return MA_ACTIVATE;

        case WM_DESTROY:
            if (g_drop.font) {
                DeleteObject(g_drop.font);
                g_drop.font = NULL;
            }
            if (g_drop.owner && IsWindow(g_drop.owner)) {
                InvalidateRect(g_drop.owner, NULL, FALSE);
                SetFocus(g_drop.owner);
            }
            memset(&g_drop, 0, sizeof(g_drop));
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int gb_dropdown_active(void)
{
    return g_drop.hwnd && IsWindow(g_drop.hwnd);
}

void gb_dropdown_close(void)
{
    if (gb_dropdown_active()) {
        DestroyWindow(g_drop.hwnd);
    }
}

int gb_dropdown_open(HWND owner, const RECT *anchor,
                     const wchar_t *const *items, int count, int selected,
                     GbPickCallback cb, void *user)
{    static int registered = 0;
    HINSTANCE inst;
    WNDCLASSEXW wc;
    RECT ar;
    RECT work;
    POINT pt;
    int width, height, x, y;
    int max_width = 0;
    int i;
    int n;
    HMONITOR mon;
    MONITORINFO mi;
    int text_px;

    if (!owner || !anchor || !items || count <= 0) return 0;

    gb_dropdown_close();

    inst = GetModuleHandleW(NULL);
    if (!registered) {
        memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = GbDropProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.lpszClassName = GB_DROP_CLASS;
        wc.style = CS_DROPSHADOW;
        if (!RegisterClassExW(&wc)) return 0;
        registered = 1;
    }

    memset(&g_drop, 0, sizeof(g_drop));
    g_drop.owner = owner;
    n = count > GB_DROP_MAX_ITEMS ? GB_DROP_MAX_ITEMS : count;
    g_drop.count = n;
    for (i = 0; i < n; i++) {
        wcsncpy(g_drop.items[i], items[i], GB_DROP_ITEM_CHARS - 1);
        g_drop.items[i][GB_DROP_ITEM_CHARS - 1] = L'\0';
    }
    g_drop.selected = (selected >= 0 && selected < n) ? selected : 0;
    g_drop.hover = g_drop.selected;
    g_drop.cb = cb;
    g_drop.user = user;
    g_drop.row_h = gb_scale(24);
    g_drop.pad = gb_scale(3);

    text_px = gb_scale(13);
    g_drop.font = CreateFontW(-text_px, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_MODERN, L"Consolas");
    if (!g_drop.font) {
        memset(&g_drop, 0, sizeof(g_drop));
        return 0;
    }

    /* Width based on the longest line */
    {
        HDC dc = GetDC(owner);
        HGDIOBJ old = SelectObject(dc, g_drop.font);
        for (i = 0; i < n; i++) {
            SIZE sz;
            if (GetTextExtentPoint32W(dc, g_drop.items[i], (int)wcslen(g_drop.items[i]), &sz)) {
                if (sz.cx > max_width) max_width = sz.cx;
            }
        }
        SelectObject(dc, old);
        ReleaseDC(owner, dc);
    }

    ar = *anchor;
    pt.x = ar.left;
    pt.y = ar.top;
    ClientToScreen(owner, &pt);

    width = ar.right - ar.left;
    if (width < max_width + gb_scale(34)) width = max_width + gb_scale(34);
    if (width < gb_scale(160)) width = gb_scale(160);
    height = n * g_drop.row_h + g_drop.pad * 2 + gb_scale(2);

    mon = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) work = mi.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);

    x = pt.x;
    y = pt.y + (ar.bottom - ar.top);
    if (x + width > work.right) x = work.right - width;
    if (x < work.left) x = work.left;
    if (y + height > work.bottom) {
        y = pt.y - height;
        if (y < work.top) y = work.top;
    }

    g_drop.hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        GB_DROP_CLASS, L"",
        WS_POPUP,
        x, y, width, height,
        owner, NULL, inst, NULL);

    if (!g_drop.hwnd) {
        DeleteObject(g_drop.font);
        memset(&g_drop, 0, sizeof(g_drop));
        return 0;
    }

    gb_round_window(g_drop.hwnd, 0);
    ShowWindow(g_drop.hwnd, SW_SHOWNOACTIVATE);
    SetForegroundWindow(g_drop.hwnd);
    SetFocus(g_drop.hwnd);
    UpdateWindow(g_drop.hwnd);
    return 1;
}

/* ------------------------------------------------------------ message box */

#define GB_MSG_CLASS L"GbMessageBoxWnd"

typedef struct {
    HWND hwnd;
    HWND owner;
    HFONT font_title;
    HFONT font_body;
    HFONT font_btn;
    RECT rc_text;
    RECT rc_icon;
    RECT rc_btn[3];
    int btn_id[3];
    int btn_count;
    int hover;
    int pressed;
    int result;
    int icon_kind;
    int default_id;
    int cancel_id;
    wchar_t title[96];
    wchar_t text[2048];
} GbMsgBox;

static GbMsgBox g_msg;

static void gb_msg_layout(void)
{
    int w;
    int h;
    int pad;
    int bw;
    int bh;
    int gap;
    int total;
    int x;
    int i;

    GetClientRect(g_msg.hwnd, (LPRECT)&g_msg.rc_text);
    w = g_msg.rc_text.right;
    h = g_msg.rc_text.bottom;
    pad = gb_scale(14);
    bw = gb_scale(92);
    bh = gb_scale(32);
    gap = gb_scale(8);

    total = g_msg.btn_count * bw + (g_msg.btn_count - 1) * gap;
    x = w - pad - total;
    for (i = 0; i < g_msg.btn_count; i++) {
        g_msg.rc_btn[i].left = x + i * (bw + gap);
        g_msg.rc_btn[i].right = g_msg.rc_btn[i].left + bw;
        g_msg.rc_btn[i].top = h - pad - bh;
        g_msg.rc_btn[i].bottom = g_msg.rc_btn[i].top + bh;
    }
}

static int gb_msg_hit(int x, int y)
{
    int i;
    POINT pt;
    pt.x = x;
    pt.y = y;
    for (i = 0; i < g_msg.btn_count; i++) {
        if (PtInRect(&g_msg.rc_btn[i], pt)) return i;
    }
    return -1;
}

static void gb_msg_activate(int index)
{
    if (index < 0 || index >= g_msg.btn_count) return;
    g_msg.result = g_msg.btn_id[index];
    DestroyWindow(g_msg.hwnd);
}

static void gb_msg_draw_button(HDC hdc, int index)
{
    RECT rc = g_msg.rc_btn[index];
    RECT body;
    RECT tr;
    const wchar_t *label = L"";
    COLORREF face, ink;
    int pressed;
    int hover;
    int shadow = gb_scale(2);
    int id = g_msg.btn_id[index];

    if (id == IDOK) label = L"OK";
    else if (id == IDYES) label = L"YES";
    else if (id == IDNO) label = L"NO";
    else if (id == IDCANCEL) label = L"CANCEL";

    pressed = (g_msg.pressed == index);
    hover = (g_msg.hover == index);
    face = GB_LIGHTEST;
    ink = GB_DARKEST;

    if (id == g_msg.default_id) {
        face = GB_DARKEST;
        ink = GB_LIGHTEST;
    }
    if (pressed) {
        face = GB_DARK;
        ink = GB_LIGHTEST;
    } else if (hover) {
        face = GB_DARK;
        ink = GB_LIGHTEST;
    }

    /* clean base on top of the background */
    gb_fill(hdc, &rc, GB_LIGHTEST);

    body = rc;
    body.right -= shadow;
    body.bottom -= shadow;
    gb_fill_round(hdc, &body, face, GB_LIGHTEST);
    gb_frame_round(hdc, &body, GB_DARKEST, gb_scale(2), GB_LIGHTEST, face, 0);

    if (!pressed) gb_shadow(hdc, &body, shadow, GB_DARK);

    tr = body;
    if (pressed) OffsetRect(&tr, 0, gb_scale(1));
    gb_text(hdc, g_msg.font_btn, ink, &tr, label,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

static void gb_msg_paint(HWND hWnd)
{
    PAINTSTRUCT ps;
    HDC hdc;
    RECT rc;
    RECT title;
    int i;
    wchar_t mark[2];

    hdc = BeginPaint(hWnd, &ps);
    GetClientRect(hWnd, &rc);
    gb_fill(hdc, &rc, GB_LIGHTEST);
    gb_frame(hdc, &rc, GB_DARKEST, gb_scale(2));

    /* Title strip */
    title = rc;
    title.left += gb_scale(2);
    title.top += gb_scale(2);
    title.right -= gb_scale(2);
    title.bottom = title.top + gb_scale(26);
    gb_fill(hdc, &title, GB_DARKEST);
    {
        RECT tr = title;
        tr.left += gb_scale(10);
        tr.right -= gb_scale(10);
        gb_text(hdc, g_msg.font_title, GB_LIGHTEST, &tr, g_msg.title,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }

    /* Icon */
    if (g_msg.icon_kind != 0) {
        gb_fill(hdc, &g_msg.rc_icon, GB_DARKEST);
        mark[0] = (g_msg.icon_kind == 2) ? L'!' : ((g_msg.icon_kind == 3) ? L'?' : L'X');
        mark[1] = L'\0';
        gb_text(hdc, g_msg.font_title, GB_LIGHTEST, &g_msg.rc_icon, mark,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    /* Body */
    gb_text(hdc, g_msg.font_body, GB_DARKEST, &g_msg.rc_text, g_msg.text,
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

    for (i = 0; i < g_msg.btn_count; i++) {
        gb_msg_draw_button(hdc, i);
    }

    EndPaint(hWnd, &ps);
}

static LRESULT CALLBACK GbMsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            gb_msg_paint(hWnd);
            return 0;

        case WM_MOUSEMOVE: {
            int idx = gb_msg_hit(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            if (idx != g_msg.hover) {
                g_msg.hover = idx;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int idx = gb_msg_hit(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            g_msg.pressed = idx;
            if (idx >= 0) InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        case WM_LBUTTONUP: {
            int idx = gb_msg_hit(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            int was = g_msg.pressed;
            g_msg.pressed = -1;
            if (idx >= 0 && idx == was) gb_msg_activate(idx);
            else InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        case WM_KEYDOWN:
            if (wParam == VK_RETURN) {
                int i;
                for (i = 0; i < g_msg.btn_count; i++) {
                    if (g_msg.btn_id[i] == g_msg.default_id) {
                        gb_msg_activate(i);
                        return 0;
                    }
                }
                gb_msg_activate(0);
                return 0;
            } else if (wParam == VK_ESCAPE) {
                int i;
                for (i = 0; i < g_msg.btn_count; i++) {
                    if (g_msg.btn_id[i] == g_msg.cancel_id) {
                        gb_msg_activate(i);
                        return 0;
                    }
                }
                gb_msg_activate(g_msg.btn_count - 1);
                return 0;
            }
            break;

        case WM_CLOSE:
            g_msg.result = g_msg.cancel_id ? g_msg.cancel_id : IDCANCEL;
            DestroyWindow(hWnd);
            return 0;

        case WM_DESTROY:
            if (g_msg.font_title) { DeleteObject(g_msg.font_title); g_msg.font_title = NULL; }
            if (g_msg.font_body) { DeleteObject(g_msg.font_body); g_msg.font_body = NULL; }
            if (g_msg.font_btn) { DeleteObject(g_msg.font_btn); g_msg.font_btn = NULL; }
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int gb_message_box(HWND owner, const wchar_t *title, const wchar_t *text, UINT flags)
{
    static int registered = 0;
    HINSTANCE inst = GetModuleHandleW(NULL);
    WNDCLASSEXW wc;
    HWND dlg;
    HDC dc;
    HGDIOBJ old;
    RECT calc;
    int body_w;
    int body_h;
    int win_w;
    int win_h;
    int title_h;
    int pad;
    int icon_w = 0;
    RECT owner_rc;
    int x, y;
    MSG msg;

    if (!title) title = L"Info";
    if (!text) text = L"";

    /* A message box can be requested while another one is already pumping
       messages (for example a download error during the Quit confirmation).
       The custom box uses one global state, so a nested request would clobber
       it. Fall back to a native box for that rare case. */
    if (g_msg.hwnd && IsWindow(g_msg.hwnd)) {
        return MessageBoxW(owner, text, title, flags);
    }

    if (!registered) {
        memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc = GbMsgProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
        wc.lpszClassName = GB_MSG_CLASS;
        wc.style = CS_DROPSHADOW;
        if (!RegisterClassExW(&wc)) return IDCANCEL;
        registered = 1;
    }

    memset(&g_msg, 0, sizeof(g_msg));
    g_msg.owner = owner;
    g_msg.hover = -1;
    g_msg.pressed = -1;
    g_msg.default_id = (flags & MB_YESNO) ? IDYES : IDOK;
    g_msg.cancel_id = (flags & MB_YESNOCANCEL) ? IDCANCEL : ((flags & MB_YESNO) ? IDNO : IDOK);
    g_msg.icon_kind = (flags & MB_ICONERROR) ? 1 : ((flags & MB_ICONWARNING) ? 2 : ((flags & MB_ICONQUESTION) ? 3 : 0));

    wcsncpy(g_msg.title, title, 95);
    g_msg.title[95] = L'\0';
    wcsncpy(g_msg.text, text, 2047);
    g_msg.text[2047] = L'\0';

    if (flags & MB_YESNOCANCEL) {
        g_msg.btn_count = 3;
        g_msg.btn_id[0] = IDYES;
        g_msg.btn_id[1] = IDNO;
        g_msg.btn_id[2] = IDCANCEL;
    } else if (flags & MB_YESNO) {
        g_msg.btn_count = 2;
        g_msg.btn_id[0] = IDYES;
        g_msg.btn_id[1] = IDNO;
    } else {
        g_msg.btn_count = 1;
        g_msg.btn_id[0] = IDOK;
    }

    g_msg.font_title = CreateFontW(-gb_scale(14), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_MODERN, L"Consolas");
    g_msg.font_body = CreateFontW(-gb_scale(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_MODERN, L"Consolas");
    g_msg.font_btn = CreateFontW(-gb_scale(13), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_MODERN, L"Consolas");

    body_w = gb_scale(390);
    dc = GetDC(owner);
    old = SelectObject(dc, g_msg.font_body);
    calc.left = 0; calc.top = 0; calc.right = body_w; calc.bottom = 0;
    DrawTextW(dc, g_msg.text, -1, &calc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    ReleaseDC(owner, dc);

    body_h = calc.bottom - calc.top;
    if (body_h < gb_scale(20)) body_h = gb_scale(20);
    if (body_h > gb_scale(230)) body_h = gb_scale(230);

    pad = gb_scale(14);
    title_h = gb_scale(26);
    if (g_msg.icon_kind) icon_w = gb_scale(30) + gb_scale(10);

    win_w = gb_scale(2) + pad + icon_w + body_w + pad + gb_scale(2);
    win_h = gb_scale(2) + title_h + pad + body_h + gb_scale(16) + gb_scale(32) + pad + gb_scale(2);

    if (owner && GetWindowRect(owner, &owner_rc)) {
        x = owner_rc.left + (owner_rc.right - owner_rc.left - win_w) / 2;
        y = owner_rc.top + (owner_rc.bottom - owner_rc.top - win_h) / 2;
    } else {
        x = (GetSystemMetrics(SM_CXSCREEN) - win_w) / 2;
        y = (GetSystemMetrics(SM_CYSCREEN) - win_h) / 2;
    }

    dlg = CreateWindowExW(
        0,
        GB_MSG_CLASS, L"",
        WS_POPUP,
        x, y, win_w, win_h,
        owner, NULL, inst, NULL);
    if (!dlg) return IDCANCEL;

    g_msg.hwnd = dlg;
    gb_msg_layout();

    g_msg.rc_icon.left = gb_scale(2) + pad;
    g_msg.rc_icon.top = gb_scale(2) + title_h + pad;
    g_msg.rc_icon.right = g_msg.rc_icon.left + gb_scale(30);
    g_msg.rc_icon.bottom = g_msg.rc_icon.top + gb_scale(30);

    g_msg.rc_text.left = gb_scale(2) + pad + icon_w;
    g_msg.rc_text.top = gb_scale(2) + title_h + pad;
    g_msg.rc_text.right = g_msg.rc_text.left + body_w;
    g_msg.rc_text.bottom = g_msg.rc_text.top + body_h;

    if (owner) EnableWindow(owner, FALSE);
    gb_round_window(dlg, 1);
    ShowWindow(dlg, SW_SHOW);
    SetForegroundWindow(dlg);
    SetFocus(dlg);
    UpdateWindow(dlg);

    while (IsWindow(dlg) && GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    return g_msg.result ? g_msg.result : IDCANCEL;
}

