#ifndef GB_UI_H
#define GB_UI_H

#include <windows.h>

/* ------------------------------------------------------------------
   Game Boy (DMG) 4 tonlu palet
   The whole interface uses only these four colors; intermediate tones are 1 px
   dama deseniyle (dither) elde edilir.
   ------------------------------------------------------------------ */
#define GB_DARKEST   RGB(0x0F, 0x38, 0x0F)
#define GB_DARK      RGB(0x30, 0x62, 0x30)
#define GB_LIGHT     RGB(0x8B, 0xAC, 0x0F)
#define GB_LIGHTEST  RGB(0x9B, 0xBC, 0x0F)

/* Corner style: stepped 45-degree cut (8-bit language), never anti-aliased. */
int  gb_radius(int large);

/* Chamfered fill and frame.
   bg: color of the area outside the corner (background).
   inner: color that fills the inside of the frame. */
void gb_fill_round(HDC hdc, const RECT *rc, COLORREF color, COLORREF bg);
void gb_frame_round(HDC hdc, const RECT *rc, COLORREF color, int thickness,
                    COLORREF bg, COLORREF inner, int large);

/* Cuts the window corners (region based; no AA). */
void gb_round_window(HWND hWnd, int large);

/* Hard (2 px) shadow shaped like the body, offset to the bottom-right.
   The shadow is clipped to the area outside the body, so
   cut corners do not get a notch. */
void gb_shadow(HDC hdc, const RECT *body, int offset, COLORREF color);

/* Owner-draw denetim rolleri */
#define GB_ROLE_PUSH     0
#define GB_ROLE_PRIMARY  1
#define GB_ROLE_DANGER   2
#define GB_ROLE_CHIP     3
#define GB_ROLE_CHECK    4
#define GB_ROLE_DROP     5
#define GB_ROLE_TITLE    6

void gb_set_dpi(UINT dpi);
int  gb_scale(int value);

void gb_fill(HDC hdc, const RECT *rc, COLORREF color);
void gb_frame(HDC hdc, const RECT *rc, COLORREF color, int thickness);
void gb_dither(HDC hdc, const RECT *rc, COLORREF color);
void gb_text(HDC hdc, HFONT font, COLORREF color, const RECT *rc,
             const wchar_t *text, UINT flags);
void gb_icon(HDC hdc, int x, int y, int scale, int kind);

/* kind: GB_ROLE_* ; checked is only meaningful for CHIP/CHECK */
void gb_draw_control(const DRAWITEMSTRUCT *dis, HFONT font, int role,
                     int checked, int hover);

/* Dropdown list (instead of a combobox). anchor is in the owner window's
   client coordinates. On selection cb(index, user) is called. */
typedef void (*GbPickCallback)(int index, void *user);
int  gb_dropdown_open(HWND owner, const RECT *anchor,
                      const wchar_t *const *items, int count, int selected,
                      GbPickCallback cb, void *user);
void gb_dropdown_close(void);
int  gb_dropdown_active(void);

/* Game Boy style modal message box.
   flags: MB_OK / MB_YESNO / MB_YESNOCANCEL + MB_ICON* */
int gb_message_box(HWND owner, const wchar_t *title, const wchar_t *text,
                   UINT flags);

#endif /* GB_UI_H */
