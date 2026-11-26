#include "wayland.h"
#include "config.h"
#include "util.h"

#include <dwmapi.h>
#include <imm.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <windowsx.h>

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

#ifndef WM_MOUSEHWHEEL
#define WM_MOUSEHWHEEL 0x020E
#endif

#ifndef MAPVK_VK_TO_CHAR
#define MAPVK_VK_TO_CHAR 2
#endif

struct WaylandApp {
    HWND hwnd;
    HDC mem_dc;
    HBITMAP dib_bmp;
    HBITMAP old_bmp;
    void *dib_bits;
    int dib_w;
    int dib_h;

    int logical_w;
    int logical_h;
    int phys_w;
    int phys_h;
    UINT dpi;

    bool dirty;
    bool quit;
    bool tracking_mouse;
    int btn_capture_count;

    TtCursorShape current_cursor;
    HCURSOR cursors[10];

    bool ime_enabled;
    int32_t ime_rect_x;
    int32_t ime_rect_y;
    int32_t ime_rect_w;
    int32_t ime_rect_h;
    bool ime_has_rect;
    wchar_t pending_high_surrogate;

    WaylandCallbacks cbs;
    void *user_ctx;
};

static const wchar_t *const WIN32_WND_CLASS = L"TuiTubeWindowClass";

static void enable_per_monitor_dpi_v2(void) {
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32) {
        typedef BOOL(WINAPI *FnSetProcessDpiAwarenessContext)(HANDLE);
        typedef BOOL(WINAPI *FnSetProcessDPIAware)(void);
        FnSetProcessDpiAwarenessContext p_set_ctx =
            (FnSetProcessDpiAwarenessContext)(void (*)(void))GetProcAddress(
                u32, "SetProcessDpiAwarenessContext");
        if (p_set_ctx && p_set_ctx((HANDLE)(intptr_t)-4)) {
            return;
        }
        FnSetProcessDPIAware p_aware =
            (FnSetProcessDPIAware)(void (*)(void))GetProcAddress(
                u32, "SetProcessDPIAware");
        if (p_aware) {
            p_aware();
        }
    }
}

static UINT query_window_dpi(HWND hwnd) {
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32) {
        typedef UINT(WINAPI *FnGetDpiForWindow)(HWND);
        FnGetDpiForWindow p_get_dpi =
            (FnGetDpiForWindow)(void (*)(void))GetProcAddress(
                u32, "GetDpiForWindow");
        if (p_get_dpi) {
            UINT d = p_get_dpi(hwnd);
            if (d >= 48u) return d;
        }
    }
    HDC hdc = GetDC(hwnd);
    UINT dpi = 96u;
    if (hdc) {
        int px = GetDeviceCaps(hdc, LOGPIXELSX);
        if (px >= 48) dpi = (UINT)px;
        ReleaseDC(hwnd, hdc);
    }
    return dpi;
}

static void destroy_dib(WaylandApp *wl) {
    if (!wl) return;
    if (wl->mem_dc && wl->old_bmp) {
        SelectObject(wl->mem_dc, wl->old_bmp);
        wl->old_bmp = NULL;
    }
    if (wl->dib_bmp) {
        DeleteObject(wl->dib_bmp);
        wl->dib_bmp = NULL;
    }
    wl->dib_bits = NULL;
    wl->dib_w = 0;
    wl->dib_h = 0;
}

static bool ensure_dib(WaylandApp *wl, int pw, int ph) {
    if (!wl || pw <= 0 || ph <= 0) return false;
    if (wl->dib_bmp && wl->dib_bits && wl->dib_w == pw && wl->dib_h == ph) {
        return true;
    }
    destroy_dib(wl);

    if (!wl->mem_dc) {
        HDC screen_dc = GetDC(wl->hwnd);
        wl->mem_dc = CreateCompatibleDC(screen_dc);
        if (screen_dc) ReleaseDC(wl->hwnd, screen_dc);
        if (!wl->mem_dc) return false;
    }

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = pw;
    bmi.bmiHeader.biHeight = -ph; /* top-down BGRA */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = NULL;
    HBITMAP bmp = CreateDIBSection(wl->mem_dc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        return false;
    }
    wl->old_bmp = (HBITMAP)SelectObject(wl->mem_dc, bmp);
    wl->dib_bmp = bmp;
    wl->dib_bits = bits;
    wl->dib_w = pw;
    wl->dib_h = ph;
    return true;
}

static void phys_to_logical(const WaylandApp *wl, int px, int py, double *out_x, double *out_y) {
    double lw = wl->logical_w > 0 ? (double)wl->logical_w : 1.0;
    double lh = wl->logical_h > 0 ? (double)wl->logical_h : 1.0;
    double pw = wl->phys_w > 0 ? (double)wl->phys_w : lw;
    double ph = wl->phys_h > 0 ? (double)wl->phys_h : lh;
    *out_x = (double)px * (lw / pw);
    *out_y = (double)py * (lh / ph);
}

static uint32_t win32_current_mods(void) {
    uint32_t mods = 0;
    if (GetKeyState(VK_CONTROL) < 0) mods |= TT_MOD_CTRL;
    if (GetKeyState(VK_SHIFT) < 0)   mods |= TT_MOD_SHIFT;
    if (GetKeyState(VK_MENU) < 0)    mods |= TT_MOD_ALT;
    if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) mods |= TT_MOD_SUPER;
    return mods;
}

static xkb_keysym_t win32_vk_to_keysym(WPARAM vk, uint32_t mods) {
    if (vk >= '0' && vk <= '9') {
        return (xkb_keysym_t)(XKB_KEY_0 + (vk - '0'));
    }
    if (vk >= 'A' && vk <= 'Z') {
        bool shift = (mods & TT_MOD_SHIFT) != 0;
        return (xkb_keysym_t)((shift ? XKB_KEY_A : XKB_KEY_a) + (vk - 'A'));
    }
    if (vk >= VK_F1 && vk <= VK_F12) {
        return (xkb_keysym_t)(XKB_KEY_F1 + (vk - VK_F1));
    }
    switch (vk) {
        case VK_BACK:     return XKB_KEY_BackSpace;
        case VK_TAB:      return XKB_KEY_Tab;
        case VK_RETURN:   return XKB_KEY_Return;
        case VK_ESCAPE:   return XKB_KEY_Escape;
        case VK_SPACE:    return XKB_KEY_space;
        case VK_PRIOR:    return XKB_KEY_Page_Up;
        case VK_NEXT:     return XKB_KEY_Page_Down;
        case VK_END:      return XKB_KEY_End;
        case VK_HOME:     return XKB_KEY_Home;
        case VK_LEFT:     return XKB_KEY_Left;
        case VK_UP:       return XKB_KEY_Up;
        case VK_RIGHT:    return XKB_KEY_Right;
        case VK_DOWN:     return XKB_KEY_Down;
        case VK_INSERT:   return XKB_KEY_Insert;
        case VK_DELETE:   return XKB_KEY_Delete;
        case VK_SHIFT:
        case VK_LSHIFT:   return XKB_KEY_Shift_L;
        case VK_RSHIFT:   return XKB_KEY_Shift_R;
        case VK_CONTROL:
        case VK_LCONTROL: return XKB_KEY_Control_L;
        case VK_RCONTROL: return XKB_KEY_Control_R;
        case VK_MENU:
        case VK_LMENU:    return XKB_KEY_Alt_L;
        case VK_RMENU:    return XKB_KEY_Alt_R;
        case VK_LWIN:     return XKB_KEY_Super_L;
        case VK_RWIN:     return XKB_KEY_Super_R;
        case VK_CAPITAL:  return XKB_KEY_Caps_Lock;
        case VK_NUMLOCK:  return XKB_KEY_Num_Lock;
        case VK_DIVIDE:   return XKB_KEY_slash;
        case VK_SUBTRACT: return XKB_KEY_minus;
        default: {
            UINT ch = MapVirtualKeyW((UINT)vk, MAPVK_VK_TO_CHAR) & 0xFFFFu;
            if (ch >= 0x20u && ch <= 0x7Eu) {
                if (ch >= 'A' && ch <= 'Z' && !(mods & TT_MOD_SHIFT)) {
                    ch = (UINT)(ch - 'A' + 'a');
                }
                return (xkb_keysym_t)ch;
            }
            return XKB_KEY_NoSymbol;
        }
    }
}

static void encode_utf8_cp(uint32_t cp, char *out, size_t cap) {
    if (!out || cap < 5) return;
    if (cp == 0 || cp > 0x10FFFFu) {
        out[0] = '\0';
        return;
    }
    if (cp <= 0x7Fu) {
        out[0] = (char)cp;
        out[1] = '\0';
    } else if (cp <= 0x7FFu) {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        out[2] = '\0';
    } else if (cp <= 0xFFFFu) {
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        out[3] = '\0';
    } else {
        out[0] = (char)(0xF0u | (cp >> 18));
        out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
        out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[3] = (char)(0x80u | (cp & 0x3Fu));
        out[4] = '\0';
    }
}

static void update_imm_cursor_pos(WaylandApp *wl) {
    if (!wl || !wl->hwnd || !wl->ime_has_rect) return;
    HIMC himc = ImmGetContext(wl->hwnd);
    if (!himc) return;

    double sx = (wl->logical_w > 0) ? ((double)wl->phys_w / (double)wl->logical_w) : 1.0;
    double sy = (wl->logical_h > 0) ? ((double)wl->phys_h / (double)wl->logical_h) : 1.0;
    int px = (int)lround((double)wl->ime_rect_x * sx);
    int py = (int)lround((double)wl->ime_rect_y * sy);
    int pw = (int)lround((double)wl->ime_rect_w * sx);
    int ph = (int)lround((double)wl->ime_rect_h * sy);
    if (pw < 1) pw = 1;
    if (ph < 1) ph = 1;

    COMPOSITIONFORM cf;
    memset(&cf, 0, sizeof(cf));
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos.x = px;
    cf.ptCurrentPos.y = py + ph;
    ImmSetCompositionWindow(himc, &cf);

    CANDIDATEFORM cand;
    memset(&cand, 0, sizeof(cand));
    cand.dwIndex = 0;
    cand.dwStyle = CFS_EXCLUDE;
    cand.ptCurrentPos.x = px;
    cand.ptCurrentPos.y = py + ph;
    cand.rcArea.left = px;
    cand.rcArea.top = py;
    cand.rcArea.right = px + pw;
    cand.rcArea.bottom = py + ph;
    ImmSetCandidateWindow(himc, &cand);

    ImmReleaseContext(wl->hwnd, himc);
}

static char *imm_get_utf8_str(HIMC himc, DWORD index, int32_t *out_byte_len) {
    if (out_byte_len) *out_byte_len = 0;
    LONG bytes = ImmGetCompositionStringW(himc, index, NULL, 0);
    if (bytes <= 0) return NULL;
    size_t wchars = (size_t)bytes / sizeof(wchar_t);
    wchar_t *wbuf = calloc(wchars + 1u, sizeof(wchar_t));
    if (!wbuf) return NULL;
    LONG got = ImmGetCompositionStringW(himc, index, wbuf, (DWORD)(wchars * sizeof(wchar_t)));
    if (got <= 0) {
        free(wbuf);
        return NULL;
    }
    wbuf[(size_t)got / sizeof(wchar_t)] = L'\0';
    char *utf8 = tt_wide_to_utf8(wbuf);
    free(wbuf);
    if (utf8 && out_byte_len) {
        *out_byte_len = (int32_t)strlen(utf8);
    }
    return utf8;
}

static int32_t imm_cursor_utf8_offset(HIMC himc, LONG wchar_pos) {
    if (wchar_pos <= 0) return 0;
    LONG bytes = ImmGetCompositionStringW(himc, GCS_COMPSTR, NULL, 0);
    if (bytes <= 0) return 0;
    size_t wchars = (size_t)bytes / sizeof(wchar_t);
    wchar_t *wbuf = calloc(wchars + 1u, sizeof(wchar_t));
    if (!wbuf) return 0;
    LONG got = ImmGetCompositionStringW(himc, GCS_COMPSTR, wbuf, (DWORD)(wchars * sizeof(wchar_t)));
    if (got <= 0) {
        free(wbuf);
        return 0;
    }
    size_t actual_wchars = (size_t)got / sizeof(wchar_t);
    size_t limit = ((size_t)wchar_pos < actual_wchars) ? (size_t)wchar_pos : actual_wchars;
    wbuf[limit] = L'\0';
    char *prefix = tt_wide_to_utf8(wbuf);
    free(wbuf);
    int32_t offset = prefix ? (int32_t)strlen(prefix) : 0;
    free(prefix);
    return offset;
}

static void handle_mouse_button(WaylandApp *wl, TtMouseButton btn, bool pressed,
                                bool dbl, LPARAM lParam) {
    if (!wl) return;
    double x = 0.0, y = 0.0;
    phys_to_logical(wl, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &x, &y);
    if (pressed) {
        if (wl->btn_capture_count == 0) SetCapture(wl->hwnd);
        wl->btn_capture_count++;
    } else {
        if (wl->btn_capture_count > 0) wl->btn_capture_count--;
        if (wl->btn_capture_count == 0 && GetCapture() == wl->hwnd) ReleaseCapture();
    }
    if (wl->cbs.on_pointer_button) {
        wl->cbs.on_pointer_button(wl->user_ctx, btn, pressed, dbl, x, y);
    }
}

static LRESULT CALLBACK win32_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    WaylandApp *wl = (WaylandApp *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_NCCREATE: {
            CREATESTRUCTW *cs = (CREATESTRUCTW *)lParam;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return TRUE;
        }
        case WM_CLOSE:
            if (wl) wl->quit = true;
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (wl && wl->mem_dc && wl->dib_bmp && wl->dib_w > 0 && wl->dib_h > 0) {
                BitBlt(hdc, 0, 0, wl->dib_w, wl->dib_h, wl->mem_dc, 0, 0, SRCCOPY);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            if (wl) {
                MINMAXINFO *mmi = (MINMAXINFO *)lParam;
                double scale = (double)(wl->dpi ? wl->dpi : 96u) / 96.0;
                RECT rc = {0, 0, (LONG)lround(640.0 * scale), (LONG)lround(400.0 * scale)};
                DWORD style = (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE);
                DWORD ex = (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
                AdjustWindowRectEx(&rc, style, FALSE, ex);
                mmi->ptMinTrackSize.x = rc.right - rc.left;
                mmi->ptMinTrackSize.y = rc.bottom - rc.top;
            }
            return 0;
        }
        case WM_DPICHANGED: {
            if (wl) {
                UINT new_dpi = HIWORD(wParam);
                if (new_dpi >= 48u) wl->dpi = new_dpi;
                const RECT *rc = (const RECT *)lParam;
                SetWindowPos(hwnd, NULL, rc->left, rc->top,
                             rc->right - rc->left, rc->bottom - rc->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                wl->dirty = true;
            }
            return 0;
        }
        case WM_SIZE: {
            if (!wl || wParam == SIZE_MINIMIZED) return 0;
            int pw = (int)LOWORD(lParam);
            int ph = (int)HIWORD(lParam);
            if (pw > 0 && ph > 0) {
                wl->phys_w = pw;
                wl->phys_h = ph;
                double scale = (double)(wl->dpi ? wl->dpi : 96u) / 96.0;
                int lw = (int)lround((double)pw / scale);
                int lh = (int)lround((double)ph / scale);
                if (lw < 1) lw = 1;
                if (lh < 1) lh = 1;
                if (lw != wl->logical_w || lh != wl->logical_h) {
                    wl->logical_w = lw;
                    wl->logical_h = lh;
                    if (wl->cbs.on_resize) {
                        wl->cbs.on_resize(wl->user_ctx, lw, lh);
                    }
                }
                wl->dirty = true;
            }
            return 0;
        }
        case WM_SETCURSOR:
            if (wl && LOWORD(lParam) == HTCLIENT) {
                size_t idx = (size_t)wl->current_cursor;
                if (idx >= sizeof(wl->cursors) / sizeof(wl->cursors[0])) idx = 0;
                SetCursor(wl->cursors[idx] ? wl->cursors[idx] : wl->cursors[0]);
                return TRUE;
            }
            break;
        case WM_MOUSEMOVE:
            if (wl) {
                if (!wl->tracking_mouse) {
                    TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
                    if (TrackMouseEvent(&tme)) wl->tracking_mouse = true;
                }
                double x = 0.0, y = 0.0;
                phys_to_logical(wl, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), &x, &y);
                if (wl->cbs.on_pointer_motion) {
                    wl->cbs.on_pointer_motion(wl->user_ctx, x, y);
                }
            }
            return 0;
        case WM_MOUSELEAVE:
            if (wl) {
                wl->tracking_mouse = false;
                if (wl->cbs.on_pointer_leave) {
                    wl->cbs.on_pointer_leave(wl->user_ctx);
                }
            }
            return 0;
        case WM_LBUTTONDOWN:
            handle_mouse_button(wl, TT_BTN_LEFT, true, false, lParam);
            return 0;
        case WM_LBUTTONDBLCLK:
            handle_mouse_button(wl, TT_BTN_LEFT, true, true, lParam);
            return 0;
        case WM_LBUTTONUP:
            handle_mouse_button(wl, TT_BTN_LEFT, false, false, lParam);
            return 0;
        case WM_RBUTTONDOWN:
            handle_mouse_button(wl, TT_BTN_RIGHT, true, false, lParam);
            return 0;
        case WM_RBUTTONUP:
            handle_mouse_button(wl, TT_BTN_RIGHT, false, false, lParam);
            return 0;
        case WM_MBUTTONDOWN:
            handle_mouse_button(wl, TT_BTN_MIDDLE, true, false, lParam);
            return 0;
        case WM_MBUTTONUP:
            handle_mouse_button(wl, TT_BTN_MIDDLE, false, false, lParam);
            return 0;
        case WM_XBUTTONDOWN: {
            WORD xb = GET_XBUTTON_WPARAM(wParam);
            TtMouseButton btn = (xb == XBUTTON1) ? TT_BTN_BACK : TT_BTN_FORWARD;
            handle_mouse_button(wl, btn, true, false, lParam);
            return TRUE;
        }
        case WM_XBUTTONUP: {
            WORD xb = GET_XBUTTON_WPARAM(wParam);
            TtMouseButton btn = (xb == XBUTTON1) ? TT_BTN_BACK : TT_BTN_FORWARD;
            handle_mouse_button(wl, btn, false, false, lParam);
            return TRUE;
        }
        case WM_MOUSEWHEEL:
            if (wl && wl->cbs.on_pointer_axis) {
                double delta = (double)GET_WHEEL_DELTA_WPARAM(wParam);
                wl->cbs.on_pointer_axis(wl->user_ctx, 0.0, -(delta / 120.0) * 15.0);
            }
            return 0;
        case WM_MOUSEHWHEEL:
            if (wl && wl->cbs.on_pointer_axis) {
                double delta = (double)GET_WHEEL_DELTA_WPARAM(wParam);
                wl->cbs.on_pointer_axis(wl->user_ctx, (delta / 120.0) * 15.0, 0.0);
            }
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            if (!wl || !wl->cbs.on_key) break;
            if (wParam == VK_PROCESSKEY || wParam == VK_PACKET) return 0;
            uint32_t mods = win32_current_mods();
            if (msg == WM_SYSKEYDOWN && wParam == VK_F4 && (mods & TT_MOD_ALT)) {
                break; /* Allow Alt+F4 to close window */
            }
            /* Suppress printable keys without shortcut modifiers; WM_CHAR delivers them */
            if ((mods & (TT_MOD_CTRL | TT_MOD_ALT | TT_MOD_SUPER)) == 0) {
                UINT mapped = MapVirtualKeyW((UINT)wParam, MAPVK_VK_TO_CHAR) & 0x7FFFFFFFu;
                if (mapped >= 0x20u && mapped != 0x7Fu) {
                    return 0;
                }
            }
            xkb_keysym_t sym = win32_vk_to_keysym(wParam, mods);
            if (sym != XKB_KEY_NoSymbol) {
                wl->cbs.on_key(wl->user_ctx, mods, sym, NULL, true);
            }
            if (msg == WM_SYSKEYDOWN) return 0;
            return 0;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            if (!wl || !wl->cbs.on_key) break;
            uint32_t mods = win32_current_mods();
            xkb_keysym_t sym = win32_vk_to_keysym(wParam, mods);
            if (sym != XKB_KEY_NoSymbol) {
                wl->cbs.on_key(wl->user_ctx, mods, sym, NULL, false);
            }
            return 0;
        }
        case WM_CHAR: {
            if (!wl || !wl->cbs.on_key) return 0;
            wchar_t wc = (wchar_t)wParam;
            uint32_t cp = 0;
            if (wc >= 0xD800u && wc <= 0xDBFFu) {
                wl->pending_high_surrogate = wc;
                return 0;
            } else if (wc >= 0xDC00u && wc <= 0xDFFFu) {
                if (wl->pending_high_surrogate) {
                    cp = 0x10000u +
                         (((uint32_t)(wl->pending_high_surrogate - 0xD800u) << 10) |
                          (uint32_t)(wc - 0xDC00u));
                    wl->pending_high_surrogate = 0;
                } else {
                    return 0;
                }
            } else {
                wl->pending_high_surrogate = 0;
                cp = (uint32_t)wc;
            }

            if (cp < 0x20u || cp == 0x7Fu) return 0;

            char utf8[8] = {0};
            encode_utf8_cp(cp, utf8, sizeof(utf8));
            if (!utf8[0]) return 0;

            uint32_t mods = win32_current_mods() & ~(TT_MOD_CTRL | TT_MOD_ALT);
            xkb_keysym_t sym = XKB_KEY_NoSymbol;
            if (cp == ' ') {
                sym = XKB_KEY_space;
            } else if (cp >= 0x21u && cp <= 0x7Eu) {
                sym = (xkb_keysym_t)cp;
            }
            wl->cbs.on_key(wl->user_ctx, mods, sym, utf8, true);
            return 0;
        }
        case WM_IME_STARTCOMPOSITION:
            if (wl) update_imm_cursor_pos(wl);
            return 0;
        case WM_IME_COMPOSITION: {
            if (!wl || !wl->cbs.on_ime) break;
            HIMC himc = ImmGetContext(hwnd);
            if (!himc) return 0;

            char *commit_str = NULL;
            char *preedit_str = NULL;
            int32_t cpos = 0;
            bool reset = false;

            if (lParam & GCS_RESULTSTR) {
                commit_str = imm_get_utf8_str(himc, GCS_RESULTSTR, NULL);
            }
            if (lParam & GCS_COMPSTR) {
                int32_t plen = 0;
                preedit_str = imm_get_utf8_str(himc, GCS_COMPSTR, &plen);
                if (preedit_str) {
                    if (lParam & GCS_CURSORPOS) {
                        LONG wpos = ImmGetCompositionStringW(himc, GCS_CURSORPOS, NULL, 0);
                        cpos = imm_cursor_utf8_offset(himc, wpos);
                    } else {
                        cpos = plen;
                    }
                }
            }
            if (lParam == 0) {
                reset = true;
            }
            ImmReleaseContext(hwnd, himc);

            WaylandImeEvent ev = {
                .preedit_text = preedit_str,
                .preedit_cursor_begin = cpos,
                .preedit_cursor_end = cpos,
                .commit_text = commit_str,
                .delete_before = 0,
                .delete_after = 0,
                .reset = reset
            };
            wl->cbs.on_ime(wl->user_ctx, &ev);
            free(preedit_str);
            free(commit_str);
            return 0;
        }
        case WM_IME_ENDCOMPOSITION:
            if (wl && wl->cbs.on_ime) {
                WaylandImeEvent ev = {.reset = true};
                wl->cbs.on_ime(wl->user_ctx, &ev);
            }
            return 0;
        case WM_KILLFOCUS:
            if (wl) {
                wl->pending_high_surrogate = 0;
                if (wl->cbs.on_ime) {
                    WaylandImeEvent ev = {.reset = true};
                    wl->cbs.on_ime(wl->user_ctx, &ev);
                }
            }
            return 0;
        default:
            break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

WaylandApp *wayland_create(int init_width, int init_height, const char *title,
                           const WaylandCallbacks *cbs, void *user_ctx,
                           char *err_buf, size_t err_cap) {
    enable_per_monitor_dpi_v2();

    WaylandApp *wl = calloc(1, sizeof(WaylandApp));
    if (!wl) return NULL;
    if (cbs) wl->cbs = *cbs;
    wl->user_ctx = user_ctx;
    wl->logical_w = init_width >= 640 ? init_width : 1024;
    wl->logical_h = init_height >= 400 ? init_height : 640;
    wl->dpi = 96u;
    wl->dirty = true;

    wl->cursors[TT_CURSOR_DEFAULT]    = LoadCursorW(NULL, IDC_ARROW);
    wl->cursors[TT_CURSOR_TEXT]       = LoadCursorW(NULL, IDC_IBEAM);
    wl->cursors[TT_CURSOR_POINTER]    = LoadCursorW(NULL, IDC_HAND);
    wl->cursors[TT_CURSOR_COL_RESIZE] = LoadCursorW(NULL, IDC_SIZEWE);
    wl->cursors[TT_CURSOR_NW_RESIZE]  = LoadCursorW(NULL, IDC_SIZENWSE);
    wl->cursors[TT_CURSOR_NE_RESIZE]  = LoadCursorW(NULL, IDC_SIZENESW);
    wl->cursors[TT_CURSOR_N_RESIZE]   = LoadCursorW(NULL, IDC_SIZENS);
    wl->cursors[TT_CURSOR_S_RESIZE]   = LoadCursorW(NULL, IDC_SIZENS);
    wl->cursors[TT_CURSOR_E_RESIZE]   = LoadCursorW(NULL, IDC_SIZEWE);
    wl->cursors[TT_CURSOR_W_RESIZE]   = LoadCursorW(NULL, IDC_SIZEWE);

    HINSTANCE hinst = GetModuleHandleW(NULL);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = win32_wndproc;
    wc.hInstance = hinst;
    wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wc.hIconSm = wc.hIcon;
    wc.hCursor = wl->cursors[TT_CURSOR_DEFAULT];
    wc.lpszClassName = WIN32_WND_CLASS;

    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "RegisterClassExW failed");
        free(wl);
        return NULL;
    }

    wchar_t *wtitle = tt_utf8_to_wide(title ? title : "TuiTube");
    DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rc = {0, 0, wl->logical_w, wl->logical_h};
    AdjustWindowRectEx(&rc, style, FALSE, 0);

    wl->hwnd = CreateWindowExW(
        0, WIN32_WND_CLASS, wtitle ? wtitle : L"TuiTube", style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, hinst, wl);
    free(wtitle);

    if (!wl->hwnd) {
        if (err_buf && err_cap) snprintf(err_buf, err_cap, "CreateWindowExW failed");
        free(wl);
        return NULL;
    }

    BOOL dark = TRUE;
    (void)DwmSetWindowAttribute(wl->hwnd, 20, &dark, sizeof(dark));

    wl->dpi = query_window_dpi(wl->hwnd);
    double scale = (double)wl->dpi / 96.0;
    int target_pw = (int)lround((double)wl->logical_w * scale);
    int target_ph = (int)lround((double)wl->logical_h * scale);
    RECT scaled_rc = {0, 0, target_pw, target_ph};
    AdjustWindowRectEx(&scaled_rc, style, FALSE, 0);
    SetWindowPos(wl->hwnd, NULL, 0, 0,
                 scaled_rc.right - scaled_rc.left, scaled_rc.bottom - scaled_rc.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    RECT crect;
    if (GetClientRect(wl->hwnd, &crect)) {
        wl->phys_w = crect.right - crect.left;
        wl->phys_h = crect.bottom - crect.top;
    } else {
        wl->phys_w = target_pw;
        wl->phys_h = target_ph;
    }

    ShowWindow(wl->hwnd, SW_SHOW);
    UpdateWindow(wl->hwnd);
    return wl;
}

void wayland_destroy(WaylandApp *wl) {
    if (!wl) return;
    destroy_dib(wl);
    if (wl->mem_dc) {
        DeleteDC(wl->mem_dc);
        wl->mem_dc = NULL;
    }
    if (wl->hwnd) {
        DestroyWindow(wl->hwnd);
        wl->hwnd = NULL;
    }
    free(wl);
}

bool wayland_needs_csd(const WaylandApp *wl) {
    (void)wl;
    return false;
}

bool wayland_is_maximized(const WaylandApp *wl) {
    return (wl && wl->hwnd) ? (IsZoomed(wl->hwnd) != 0) : false;
}

bool wayland_should_quit(const WaylandApp *wl) {
    return wl ? wl->quit : true;
}

void wayland_request_quit(WaylandApp *wl) {
    if (wl) wl->quit = true;
}

void wayland_request_redraw(WaylandApp *wl) {
    if (wl) wl->dirty = true;
}

void wayland_set_opaque(WaylandApp *wl, bool opaque) {
    (void)wl;
    (void)opaque;
}

void wayland_set_cursor(WaylandApp *wl, TtCursorShape shape) {
    if (!wl || wl->current_cursor == shape) return;
    wl->current_cursor = shape;
    size_t idx = (size_t)shape;
    if (idx >= sizeof(wl->cursors) / sizeof(wl->cursors[0])) idx = 0;
    SetCursor(wl->cursors[idx] ? wl->cursors[idx] : wl->cursors[0]);
}

void wayland_start_move(WaylandApp *wl) {
    if (!wl || !wl->hwnd) return;
    ReleaseCapture();
    SendMessageW(wl->hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
}

void wayland_start_resize(WaylandApp *wl, uint32_t edges) {
    if (!wl || !wl->hwnd) return;
    WPARAM sc = 0;
    switch (edges) {
        case TT_RESIZE_LEFT:         sc = 1; break;
        case TT_RESIZE_RIGHT:        sc = 2; break;
        case TT_RESIZE_TOP:          sc = 3; break;
        case TT_RESIZE_TOP_LEFT:     sc = 4; break;
        case TT_RESIZE_TOP_RIGHT:    sc = 5; break;
        case TT_RESIZE_BOTTOM:       sc = 6; break;
        case TT_RESIZE_BOTTOM_LEFT:  sc = 7; break;
        case TT_RESIZE_BOTTOM_RIGHT: sc = 8; break;
        default: return;
    }
    ReleaseCapture();
    SendMessageW(wl->hwnd, WM_SYSCOMMAND, SC_SIZE + sc, 0);
}

void wayland_toggle_maximize(WaylandApp *wl) {
    if (!wl || !wl->hwnd) return;
    ShowWindow(wl->hwnd, IsZoomed(wl->hwnd) ? SW_RESTORE : SW_MAXIMIZE);
}

void wayland_minimize(WaylandApp *wl) {
    if (!wl || !wl->hwnd) return;
    ShowWindow(wl->hwnd, SW_MINIMIZE);
}

bool wayland_clipboard_copy(WaylandApp *wl, const char *text) {
    if (!text) return false;
    wchar_t *w = tt_utf8_to_wide(text);
    if (!w) return false;
    if (!OpenClipboard(wl ? wl->hwnd : NULL)) {
        free(w);
        return false;
    }
    EmptyClipboard();
    size_t bytes = (wcslen(w) + 1u) * sizeof(wchar_t);
    HGLOBAL hmem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hmem) {
        CloseClipboard();
        free(w);
        return false;
    }
    void *dst = GlobalLock(hmem);
    if (!dst) {
        GlobalFree(hmem);
        CloseClipboard();
        free(w);
        return false;
    }
    memcpy(dst, w, bytes);
    GlobalUnlock(hmem);
    free(w);
    if (!SetClipboardData(CF_UNICODETEXT, hmem)) {
        GlobalFree(hmem);
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

char *wayland_clipboard_paste(WaylandApp *wl) {
    if (!OpenClipboard(wl ? wl->hwnd : NULL)) return NULL;
    HANDLE hdata = GetClipboardData(CF_UNICODETEXT);
    if (!hdata) {
        CloseClipboard();
        return NULL;
    }
    const wchar_t *w = (const wchar_t *)GlobalLock(hdata);
    char *utf8 = w ? tt_wide_to_utf8(w) : NULL;
    if (w) GlobalUnlock(hdata);
    CloseClipboard();
    if (!utf8) return NULL;

    /* Strip \r from Windows CRLF clipboard text */
    size_t r = 0, widx = 0;
    for (; utf8[r] != '\0'; r++) {
        if (utf8[r] != '\r') utf8[widx++] = utf8[r];
    }
    utf8[widx] = '\0';
    return utf8;
}

void wayland_ime_set_enabled(WaylandApp *wl, bool enabled) {
    if (!wl) return;
    wl->ime_enabled = enabled;
    if (!wl->hwnd) return;
    ImmAssociateContextEx(wl->hwnd, NULL, enabled ? IACE_DEFAULT : 0);
}

void wayland_ime_set_surrounding(WaylandApp *wl, const char *text,
                                 int32_t cursor_byte, int32_t anchor_byte,
                                 bool external_change) {
    (void)wl;
    (void)text;
    (void)cursor_byte;
    (void)anchor_byte;
    (void)external_change;
}

void wayland_ime_set_cursor_rect(WaylandApp *wl, int32_t x, int32_t y,
                                 int32_t w, int32_t h) {
    if (!wl) return;
    wl->ime_rect_x = x;
    wl->ime_rect_y = y;
    wl->ime_rect_w = w > 0 ? w : 1;
    wl->ime_rect_h = h > 0 ? h : 1;
    wl->ime_has_rect = true;
    update_imm_cursor_pos(wl);
}

int win32_poll_once(WaylandApp *wl, void *extra_event, int timeout_ms, bool *out_extra_ready) {
    if (out_extra_ready) *out_extra_ready = false;
    if (!wl) return -1;

    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            wl->quit = true;
        } else {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (extra_event && WaitForSingleObject((HANDLE)extra_event, 0) == WAIT_OBJECT_0) {
        if (out_extra_ready) *out_extra_ready = true;
        return 0;
    }

    if (wl->quit || wl->dirty) {
        return 0;
    }

    HANDLE handles[1];
    DWORD count = 0;
    if (extra_event) {
        handles[0] = (HANDLE)extra_event;
        count = 1;
    }
    DWORD wait_ms = (timeout_ms < 0) ? INFINITE : (DWORD)timeout_ms;
    DWORD res = MsgWaitForMultipleObjects(count, count ? handles : NULL,
                                          FALSE, wait_ms, QS_ALLINPUT);
    if (res == WAIT_FAILED) {
        return -1;
    }

    if (extra_event && (res == WAIT_OBJECT_0 ||
                        WaitForSingleObject((HANDLE)extra_event, 0) == WAIT_OBJECT_0)) {
        if (out_extra_ready) *out_extra_ready = true;
    }

    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            wl->quit = true;
        } else {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}

int wayland_poll_once(WaylandApp *wl, int extra_fd, int timeout_ms, bool *out_extra_ready) {
    (void)extra_fd;
    return win32_poll_once(wl, NULL, timeout_ms, out_extra_ready);
}

void wayland_render_if_needed(WaylandApp *wl) {
    if (!wl || !wl->dirty || wl->phys_w <= 0 || wl->phys_h <= 0 ||
        !wl->hwnd || IsIconic(wl->hwnd)) {
        return;
    }
    if (!ensure_dib(wl, wl->phys_w, wl->phys_h)) {
        return;
    }

    cairo_surface_t *surf = cairo_image_surface_create_for_data(
        (unsigned char *)wl->dib_bits, CAIRO_FORMAT_ARGB32,
        wl->phys_w, wl->phys_h, wl->phys_w * 4);
    if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        if (surf) cairo_surface_destroy(surf);
        return;
    }

    cairo_t *cr = cairo_create(surf);
    double sx = wl->logical_w > 0 ? ((double)wl->phys_w / (double)wl->logical_w) : 1.0;
    double sy = wl->logical_h > 0 ? ((double)wl->phys_h / (double)wl->logical_h) : 1.0;
    cairo_scale(cr, sx, sy);

    if (wl->cbs.on_render) {
        wl->cbs.on_render(wl->user_ctx, cr, wl->logical_w, wl->logical_h);
    }

    cairo_surface_flush(surf);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);

    wl->dirty = false;
    InvalidateRect(wl->hwnd, NULL, FALSE);
    UpdateWindow(wl->hwnd);
}
