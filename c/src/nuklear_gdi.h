/*
 * Minimal Nuklear Win32 GDI backend
 * Public domain / MIT
 */
#ifndef NUKLEAR_GDI_H
#define NUKLEAR_GDI_H

#include <windows.h>

typedef struct GdiFont GdiFont;

/* Create window and Nuklear context. Returns HWND. */
HWND nk_gdi_create_window(HINSTANCE hInstance, const char *title,
                          int width, int height,
                          GdiFont **font, struct nk_context **ctx);

/* Render the Nuklear draw commands */
void nk_gdi_render(struct nk_context *ctx, enum nk_anti_aliasing aa);

/* Shutdown and cleanup */
void nk_gdi_shutdown(void);

/* Handle Win32 messages - call from your WndProc */
LRESULT nk_gdi_handle_event(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                           struct nk_context *ctx);

#ifdef NK_GDI_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>

struct GdiFont {
    struct nk_user_font nk;
    HFONT handle;
    int height;
};

static float nk_gdi_font_width(nk_handle handle, float height,
                               const char *text, int len) {
    GdiFont *font = (GdiFont*)handle.ptr;
    if (!text || len <= 0) return 0;
    SIZE size;
    /* Convert UTF-8 to wide for measurement - simple ASCII path */
    if (GetTextExtentPoint32A(g_gdi.memdc, text, len, &size))
        return (float)size.cx;
    return (float)(len * font->height / 2);
}

static struct {
    HWND hwnd;
    HDC hdc;
    HDC memdc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    int width, height;
    struct nk_context ctx;
    struct nk_buffer cmds;
    GdiFont font;
} g_gdi;

static LRESULT CALLBACK nk_gdi_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    return nk_gdi_handle_event(hwnd, msg, wparam, lparam, &g_gdi.ctx);
}

HWND nk_gdi_create_window(HINSTANCE hInstance, const char *title,
                          int width, int height,
                          GdiFont **font, struct nk_context **ctx) {
    WNDCLASSA wc = {0};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = nk_gdi_wndproc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = "mp3packer_plus_gui";
    RegisterClassA(&wc);
    
    RECT rect = {0, 0, width, height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    
    g_gdi.hwnd = CreateWindowA(wc.lpszClassName, title,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, hInstance, NULL);
    
    if (!g_gdi.hwnd) return NULL;
    
    g_gdi.width = width;
    g_gdi.height = height;
    g_gdi.hdc = GetDC(g_gdi.hwnd);
    g_gdi.memdc = CreateCompatibleDC(g_gdi.hdc);
    g_gdi.bitmap = CreateCompatibleBitmap(g_gdi.hdc, width, height);
    g_gdi.old_bitmap = SelectObject(g_gdi.memdc, g_gdi.bitmap);
    
    /* Create font */
    g_gdi.font.handle = CreateFontA(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH, "Segoe UI");
    if (!g_gdi.font.handle)
        g_gdi.font.handle = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    g_gdi.font.height = 16;
    g_gdi.font.nk.userdata = nk_handle_ptr(&g_gdi.font);
    g_gdi.font.nk.height = 16;
    g_gdi.font.nk.width = nk_gdi_font_width;
    
    nk_init_default(&g_gdi.ctx, &g_gdi.font.nk);
    nk_buffer_init_default(&g_gdi.cmds);
    
    *font = &g_gdi.font;
    *ctx = &g_gdi.ctx;
    
    return g_gdi.hwnd;
}

LRESULT nk_gdi_handle_event(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                            struct nk_context *ctx) {
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SIZE: {
        int w = LOWORD(lparam);
        int h = HIWORD(lparam);
        if (w != g_gdi.width || h != g_gdi.height) {
            SelectObject(g_gdi.memdc, g_gdi.old_bitmap);
            DeleteObject(g_gdi.bitmap);
            g_gdi.width = w;
            g_gdi.height = h;
            g_gdi.bitmap = CreateCompatibleBitmap(g_gdi.hdc, w, h);
            g_gdi.old_bitmap = SelectObject(g_gdi.memdc, g_gdi.bitmap);
        }
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        int down = !(lparam >> 31 & 1);
        int ctrl = GetKeyState(VK_CONTROL) & (1<<15);
        nk_input_key(ctx, NK_KEY_CTRL, ctrl);
        switch (wparam) {
        case VK_SHIFT: nk_input_key(ctx, NK_KEY_SHIFT, down); break;
        case VK_DELETE: nk_input_key(ctx, NK_KEY_DEL, down); break;
        case VK_RETURN: nk_input_key(ctx, NK_KEY_ENTER, down); break;
        case VK_TAB: nk_input_key(ctx, NK_KEY_TAB, down); break;
        case VK_LEFT: nk_input_key(ctx, NK_KEY_LEFT, down); break;
        case VK_RIGHT: nk_input_key(ctx, NK_KEY_RIGHT, down); break;
        case VK_UP: nk_input_key(ctx, NK_KEY_UP, down); break;
        case VK_DOWN: nk_input_key(ctx, NK_KEY_DOWN, down); break;
        case VK_HOME: nk_input_key(ctx, NK_KEY_TEXT_START, down); break;
        case VK_END: nk_input_key(ctx, NK_KEY_TEXT_END, down); break;
        case 'C': if (ctrl) nk_input_key(ctx, NK_KEY_COPY, down); break;
        case 'V': if (ctrl) nk_input_key(ctx, NK_KEY_PASTE, down); break;
        case 'X': if (ctrl) nk_input_key(ctx, NK_KEY_CUT, down); break;
        case 'Z': if (ctrl) nk_input_key(ctx, NK_KEY_TEXT_UNDO, down); break;
        }
        return 0;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        int down = !(lparam >> 31 & 1);
        int ctrl = GetKeyState(VK_CONTROL) & (1<<15);
        nk_input_key(ctx, NK_KEY_CTRL, ctrl);
        switch (wparam) {
        case VK_SHIFT: nk_input_key(ctx, NK_KEY_SHIFT, down); break;
        case VK_DELETE: nk_input_key(ctx, NK_KEY_DEL, down); break;
        case VK_RETURN: nk_input_key(ctx, NK_KEY_ENTER, down); break;
        case VK_TAB: nk_input_key(ctx, NK_KEY_TAB, down); break;
        case VK_LEFT: nk_input_key(ctx, NK_KEY_LEFT, down); break;
        case VK_RIGHT: nk_input_key(ctx, NK_KEY_RIGHT, down); break;
        case VK_UP: nk_input_key(ctx, NK_KEY_UP, down); break;
        case VK_DOWN: nk_input_key(ctx, NK_KEY_DOWN, down); break;
        }
        return 0;
    }
    case WM_CHAR:
        if (wparam >= 32) {
            nk_input_unicode(ctx, (nk_rune)wparam);
        }
        return 0;
    case WM_LBUTTONDOWN:
        nk_input_button(ctx, NK_BUTTON_LEFT, 
            (short)LOWORD(lparam), (short)HIWORD(lparam), 1);
        SetCapture(hwnd);
        return 0;
    case WM_LBUTTONUP:
        nk_input_button(ctx, NK_BUTTON_DOUBLE,
            (short)LOWORD(lparam), (short)HIWORD(lparam), 0);
        nk_input_button(ctx, NK_BUTTON_LEFT,
            (short)LOWORD(lparam), (short)HIWORD(lparam), 0);
        ReleaseCapture();
        return 0;
    case WM_RBUTTONDOWN:
        nk_input_button(ctx, NK_BUTTON_RIGHT,
            (short)LOWORD(lparam), (short)HIWORD(lparam), 1);
        SetCapture(hwnd);
        return 0;
    case WM_RBUTTONUP:
        nk_input_button(ctx, NK_BUTTON_RIGHT,
            (short)LOWORD(lparam), (short)HIWORD(lparam), 0);
        ReleaseCapture();
        return 0;
    case WM_MOUSEMOVE:
        nk_input_motion(ctx, (short)LOWORD(lparam), (short)HIWORD(lparam));
        return 0;
    case WM_MOUSEWHEEL:
        nk_input_scroll(ctx, nk_vec2(0, (float)(short)HIWORD(wparam) / WHEEL_DELTA));
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

static COLORREF nk_to_colorref(struct nk_color c) {
    return RGB(c.r, c.g, c.b);
}

void nk_gdi_render(struct nk_context *ctx, enum nk_anti_aliasing aa) {
    (void)aa;
    
    /* Clear */
    RECT rect = {0, 0, g_gdi.width, g_gdi.height};
    FillRect(g_gdi.memdc, &rect, (HBRUSH)(COLOR_WINDOW+1));
    
    SelectObject(g_gdi.memdc, g_gdi.font.handle);
    SetBkMode(g_gdi.memdc, TRANSPARENT);
    
    /* Draw commands */
    const struct nk_command *cmd;
    nk_foreach(cmd, ctx) {
        switch (cmd->type) {
        case NK_COMMAND_NOP: break;
        case NK_COMMAND_SCISSOR: {
            const struct nk_command_scissor *s = (const struct nk_command_scissor*)cmd;
            HRGN rgn = CreateRectRgn(s->x, s->y, s->x + s->w, s->y + s->h);
            SelectClipRgn(g_gdi.memdc, rgn);
            DeleteObject(rgn);
            break;
        }
        case NK_COMMAND_LINE: {
            const struct nk_command_line *l = (const struct nk_command_line*)cmd;
            HPEN pen = CreatePen(PS_SOLID, l->line_thickness, nk_to_colorref(l->color));
            HPEN old = SelectObject(g_gdi.memdc, pen);
            MoveToEx(g_gdi.memdc, l->begin.x, l->begin.y, NULL);
            LineTo(g_gdi.memdc, l->end.x, l->end.y);
            SelectObject(g_gdi.memdc, old);
            DeleteObject(pen);
            break;
        }
        case NK_COMMAND_RECT: {
            const struct nk_command_rect *r = (const struct nk_command_rect*)cmd;
            HPEN pen = CreatePen(PS_SOLID, r->line_thickness, nk_to_colorref(r->color));
            HBRUSH brush = (HBRUSH)GetStockObject(NULL_BRUSH);
            HPEN old_pen = SelectObject(g_gdi.memdc, pen);
            HBRUSH old_brush = SelectObject(g_gdi.memdc, brush);
            Rectangle(g_gdi.memdc, r->x, r->y, r->x + r->w, r->y + r->h);
            SelectObject(g_gdi.memdc, old_pen);
            SelectObject(g_gdi.memdc, old_brush);
            DeleteObject(pen);
            break;
        }
        case NK_COMMAND_RECT_FILLED: {
            const struct nk_command_rect_filled *r = (const struct nk_command_rect_filled*)cmd;
            HBRUSH brush = CreateSolidBrush(nk_to_colorref(r->color));
            RECT rc = {r->x, r->y, r->x + r->w, r->y + r->h};
            FillRect(g_gdi.memdc, &rc, brush);
            DeleteObject(brush);
            break;
        }
        case NK_COMMAND_CIRCLE: {
            const struct nk_command_circle *c = (const struct nk_command_circle*)cmd;
            HPEN pen = CreatePen(PS_SOLID, c->line_thickness, nk_to_colorref(c->color));
            HBRUSH brush = (HBRUSH)GetStockObject(NULL_BRUSH);
            HPEN old_pen = SelectObject(g_gdi.memdc, pen);
            HBRUSH old_brush = SelectObject(g_gdi.memdc, brush);
            Ellipse(g_gdi.memdc, c->x, c->y, c->x + c->w, c->y + c->h);
            SelectObject(g_gdi.memdc, old_pen);
            SelectObject(g_gdi.memdc, old_brush);
            DeleteObject(pen);
            break;
        }
        case NK_COMMAND_CIRCLE_FILLED: {
            const struct nk_command_circle_filled *c = (const struct nk_command_circle_filled*)cmd;
            HBRUSH brush = CreateSolidBrush(nk_to_colorref(c->color));
            HPEN pen = CreatePen(PS_SOLID, 1, nk_to_colorref(c->color));
            HPEN old_pen = SelectObject(g_gdi.memdc, pen);
            HBRUSH old_brush = SelectObject(g_gdi.memdc, brush);
            Ellipse(g_gdi.memdc, c->x, c->y, c->x + c->w, c->y + c->h);
            SelectObject(g_gdi.memdc, old_pen);
            SelectObject(g_gdi.memdc, old_brush);
            DeleteObject(pen);
            DeleteObject(brush);
            break;
        }
        case NK_COMMAND_TEXT: {
            const struct nk_command_text *t = (const struct nk_command_text*)cmd;
            SetTextColor(g_gdi.memdc, nk_to_colorref(t->foreground));
            /* Convert UTF-8 to wide char for TextOut */
            int len = t->length;
            /* Simple: assume ASCII for now */
            TextOutA(g_gdi.memdc, t->x, t->y, t->string, len);
            break;
        }
        default: break;
        }
    }
    
    nk_clear(ctx);
    
    /* Blit to screen */
    BitBlt(g_gdi.hdc, 0, 0, g_gdi.width, g_gdi.height, g_gdi.memdc, 0, 0, SRCCOPY);
}

void nk_gdi_shutdown(void) {
    SelectObject(g_gdi.memdc, g_gdi.old_bitmap);
    DeleteObject(g_gdi.bitmap);
    DeleteDC(g_gdi.memdc);
    ReleaseDC(g_gdi.hwnd, g_gdi.hdc);
    DeleteObject(g_gdi.font.handle);
    nk_buffer_free(&g_gdi.cmds);
    nk_free(&g_gdi.ctx);
}

#endif /* NK_GDI_IMPLEMENTATION */
#endif /* NUKLEAR_GDI_H */
