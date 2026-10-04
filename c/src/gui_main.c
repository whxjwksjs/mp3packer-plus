/*
 * mp3packer-plus GUI — Windows x64
 * 
 * Simple native-looking GUI using Nuklear (single-header, public domain)
 * with Win32 GDI backend. No dependencies, small binary.
 *
 * Based on the classic mp3packer by Reed Wilson ("Omion"). GPL-2.0.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_IMPLEMENTATION
#define NK_GDI_IMPLEMENTATION
#include "nuklear.h"
#include "nuklear_gdi.h"

/* Engine headers */
#include "mp3packer.h"
#include "mp3queue.h"
#include "mp3parse.h"

/* Window state */
static struct {
    char input_path[MAX_PATH];
    char output_path[MAX_PATH];
    int min_bitrate;
    int delete_id3v2;
    int delete_trailing;
    int minimize_reservoir;
    int running;
    float progress;
    char log[8192];
    char status[256];
} g_ui;

/* Append to log */
static void log_msg(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    
    size_t len = strlen(g_ui.log);
    size_t blen = strlen(buf);
    if (len + blen + 2 < sizeof(g_ui.log)) {
        strcat(g_ui.log, buf);
        strcat(g_ui.log, "\n");
    }
}

/* File picker */
static int pick_file(char *out, const char *filter, int save) {
    OPENFILENAMEA ofn;
    char buf[MAX_PATH] = {0};
    if (!save && g_ui.input_path[0])
        strncpy(buf, g_ui.input_path, MAX_PATH-1);
    
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    
    int ok = save ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
    if (ok) {
        strncpy(out, buf, MAX_PATH-1);
        return 1;
    }
    return 0;
}

/* Repack worker - runs the engine */
static DWORD WINAPI repack_thread(LPVOID param) {
    (void)param;
    
    g_ui.running = 1;
    g_ui.progress = 0.0f;
    strcpy(g_ui.status, "Reading input...");
    log_msg("Input: %s", g_ui.input_path);
    log_msg("Output: %s", g_ui.output_path);
    
    /* Read input file */
    FILE *f = fopen(g_ui.input_path, "rb");
    if (!f) {
        strcpy(g_ui.status, "Error: cannot read input");
        log_msg("ERROR: Cannot open input file");
        g_ui.running = 0;
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long in_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *in_data = malloc(in_len);
    fread(in_data, 1, in_len, f);
    fclose(f);
    
    /* Find first frame */
    size_t offset = 0;
    if (mp3_find_sync(in_data, in_len, &offset, NULL) != 0) {
        strcpy(g_ui.status, "Error: no MP3 frames found");
        log_msg("ERROR: No MP3 frames found");
        free(in_data);
        g_ui.running = 0;
        return 1;
    }
    
    /* Set up repacker */
    repacker_t rq;
    repacker_init(&rq);
    rq.in_data = in_data;
    rq.in_len = in_len;
    rq.min_bitrate = g_ui.min_bitrate;
    rq.minimize_reservoir = g_ui.minimize_reservoir;
    rq.delete_leading = g_ui.delete_id3v2;
    rq.delete_trailing = g_ui.delete_trailing;
    
    if (offset > 0 && !rq.delete_leading) {
        rq.leading_junk = in_data;
        rq.leading_junk_len = offset;
        log_msg("Preserving %zu bytes of leading data (ID3v2)", offset);
    }
    
    /* Parse frames */
    strcpy(g_ui.status, "Parsing frames...");
    size_t pos = offset;
    parsed_frame_t pf;
    size_t last_end = offset;
    size_t nframes = 0;
    
    while (pos < (size_t)in_len) {
        int fsize = mp3_parse_frame(in_data, in_len, pos, &pf);
        if (fsize < 0) break;
        
        /* Skip Xing/Info frames */
        int si_size = mp3_side_info_size(&pf.header);
        int xing_off = 4 + (pf.header.protection ? 0 : 2) + si_size;
        if (pos + xing_off + 4 <= (size_t)in_len) {
            const uint8_t *xp = in_data + pos + xing_off;
            if ((xp[0]=='X'&&xp[1]=='i'&&xp[2]=='n'&&xp[3]=='g') ||
                (xp[0]=='I'&&xp[1]=='n'&&xp[2]=='f'&&xp[3]=='o')) {
                last_end = pos + fsize;
                pos += fsize;
                continue;
            }
        }
        
        if (repacker_add_frame(&rq, &pf) != 0) break;
        last_end = pos + fsize;
        pos += fsize;
        nframes++;
        
        if (nframes % 500 == 0) {
            g_ui.progress = (float)pos / (float)in_len * 0.5f;
        }
    }
    
    log_msg("Parsed %zu frames", nframes);
    
    if (last_end < (size_t)in_len && !rq.delete_trailing) {
        rq.trailing_junk = in_data + last_end;
        rq.trailing_junk_len = in_len - last_end;
        log_msg("Preserving %zu bytes of trailing data", in_len - last_end);
    }
    
    /* Run repacker */
    strcpy(g_ui.status, "Repacking...");
    g_ui.progress = 0.5f;
    
    if (repacker_run(&rq) != 0) {
        strcpy(g_ui.status, "Error: repacking failed");
        log_msg("ERROR: Repacking failed");
        repacker_free(&rq);
        free(in_data);
        g_ui.running = 0;
        return 1;
    }
    
    g_ui.progress = 0.9f;
    
    /* Write output */
    strcpy(g_ui.status, "Writing output...");
    f = fopen(g_ui.output_path, "wb");
    if (!f) {
        strcpy(g_ui.status, "Error: cannot write output");
        log_msg("ERROR: Cannot open output file for writing");
        repacker_free(&rq);
        free(in_data);
        g_ui.running = 0;
        return 1;
    }
    fwrite(rq.out_data, 1, rq.out_len, f);
    fclose(f);
    
    g_ui.progress = 1.0f;
    
    /* Report */
    long out_len = (long)rq.out_len;
    double saved_pct = 100.0 * (1.0 - (double)out_len / (double)in_len);
    snprintf(g_ui.status, sizeof(g_ui.status), 
             "Done: %ld -> %ld bytes (%.1f%%)", in_len, out_len, saved_pct);
    log_msg("Wrote %ld bytes (input %ld)", out_len, in_len);
    log_msg("Saved %.1f%%", saved_pct);
    
    repacker_free(&rq);
    free(in_data);
    g_ui.running = 0;
    return 0;
}

/* Main window */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine;
    
    /* Initialize UI state */
    memset(&g_ui, 0, sizeof(g_ui));
    strcpy(g_ui.status, "Ready");
    strcpy(g_ui.log, "mp3packer-plus GUI v0.1.0\nBased on mp3packer by Reed Wilson (Omion)\n\n");
    
    /* Create window */
    GdiFont *font;
    struct nk_context *ctx;
    HWND hwnd = nk_gdi_create_window(hInstance, "mp3packer-plus", 640, 520, &font, &ctx);
    if (!hwnd) return 1;
    
    ShowWindow(hwnd, nCmdShow);
    
    /* Main loop */
    int running = 1;
    while (running) {
        /* Input */
        MSG msg;
        nk_input_begin(ctx);
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = 0;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        nk_input_end(ctx);
        
        /* GUI */
        if (nk_begin(ctx, "mp3packer-plus", nk_rect(0, 0, 640, 520),
                     NK_WINDOW_BORDER|NK_WINDOW_TITLE)) {
            
            nk_layout_row_static(ctx, 24, 600, 1);
            nk_label(ctx, "Input MP3 file:", NK_TEXT_LEFT);
            
            nk_layout_row_begin(ctx, NK_STATIC, 28, 2);
            nk_layout_row_push(ctx, 480);
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, 
                g_ui.input_path, MAX_PATH, nk_filter_default);
            nk_layout_row_push(ctx, 100);
            if (nk_button_label(ctx, "Browse...")) {
                pick_file(g_ui.input_path, 
                    "MP3 files\0*.mp3\0All files\0*.*\0", 0);
                /* Auto-suggest output path */
                if (g_ui.input_path[0] && !g_ui.output_path[0]) {
                    strncpy(g_ui.output_path, g_ui.input_path, MAX_PATH-1);
                    char *dot = strrchr(g_ui.output_path, '.');
                    if (dot) strcpy(dot, "_repacked.mp3");
                }
            }
            nk_layout_row_end(ctx);
            
            nk_layout_row_static(ctx, 24, 600, 1);
            nk_label(ctx, "Output MP3 file:", NK_TEXT_LEFT);
            
            nk_layout_row_begin(ctx, NK_STATIC, 28, 2);
            nk_layout_row_push(ctx, 480);
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD,
                g_ui.output_path, MAX_PATH, nk_filter_default);
            nk_layout_row_push(ctx, 100);
            if (nk_button_label(ctx, "Browse...")) {
                pick_file(g_ui.output_path,
                    "MP3 files\0*.mp3\0All files\0*.*\0", 1);
            }
            nk_layout_row_end(ctx);
            
            nk_layout_row_static(ctx, 24, 600, 1);
            nk_label(ctx, "Options:", NK_TEXT_LEFT);
            
            nk_layout_row_dynamic(ctx, 24, 2);
            nk_checkbox_label(ctx, "Strip ID3v2 (leading)", &g_ui.delete_id3v2);
            nk_checkbox_label(ctx, "Strip trailing data", &g_ui.delete_trailing);
            
            nk_layout_row_dynamic(ctx, 24, 2);
            nk_checkbox_label(ctx, "Minimize reservoir (-r)", &g_ui.minimize_reservoir);
            
            nk_layout_row_begin(ctx, NK_STATIC, 24, 2);
            nk_layout_row_push(ctx, 200);
            nk_label(ctx, "Minimum bitrate (kbps, 0=none):", NK_TEXT_LEFT);
            nk_layout_row_push(ctx, 100);
            nk_property_int(ctx, "#Bitrate:", 0, &g_ui.min_bitrate, 320, 8, 1);
            nk_layout_row_end(ctx);
            
            nk_layout_row_static(ctx, 12, 600, 1);
            
            /* Progress */
            nk_layout_row_dynamic(ctx, 24, 1);
            nk_progress(ctx, (nk_size*)&g_ui.progress, 100, NK_FIXED);
            
            nk_layout_row_static(ctx, 24, 600, 1);
            nk_label(ctx, g_ui.status, NK_TEXT_LEFT);
            
            /* Start button */
            nk_layout_row_static(ctx, 36, 120, 1);
            if (!g_ui.running) {
                if (nk_button_label(ctx, "Repack")) {
                    if (!g_ui.input_path[0] || !g_ui.output_path[0]) {
                        strcpy(g_ui.status, "Please select input and output files");
                    } else {
                        g_ui.log[0] = 0;
                        log_msg("Starting repack...");
                        CreateThread(NULL, 0, repack_thread, NULL, 0, NULL);
                    }
                }
            } else {
                nk_label(ctx, "Working...", NK_TEXT_LEFT);
            }
            
            /* Log output */
            nk_layout_row_static(ctx, 24, 600, 1);
            nk_label(ctx, "Log:", NK_TEXT_LEFT);
            nk_layout_row_dynamic(ctx, 140, 1);
            nk_edit_string_zero_terminated(ctx, 
                NK_EDIT_MULTILINE|NK_EDIT_READ_ONLY,
                g_ui.log, sizeof(g_ui.log), nk_filter_default);
        }
        nk_end(ctx);
        
        /* Render */
        nk_gdi_render(ctx, NK_ANTI_ALIASING_ON);
    }
    
    nk_gdi_shutdown();
    return 0;
}
