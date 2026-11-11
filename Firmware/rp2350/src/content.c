#include "content.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void note(Settings *s, const char *msg) {
    s->warnings++;
    if (s->warn) s->warn(msg);
}

static int yes(const char *v) {
    return !strcmp(v, "yes") || !strcmp(v, "y") || !strcmp(v, "true") ||
           !strcmp(v, "on") || !strcmp(v, "1");
}

void settings_defaults(Settings *s) {
    void (*warn)(const char *) = s->warn;
    int warnings = s->warnings;
    memset(s, 0, sizeof *s);
    s->warn = warn;
    s->warnings = warnings;
    s->show = SHOW_TEXT;
    s->seconds = 4.0;
    s->stretch = 1.0;
    s->lit_dark = 1;
    s->passes = 0;
    s->sign = 1;
    s->shift_ms = 6.0;
    s->anchor = ANCHOR_PEAKS;
    s->width = 60.0;
    s->sleep_on = 1;
    s->sleep_s = 3.0;
    memcpy(s->sleep_mode, "light", 6);
    s->wake_g = 0.25;
}

void settings_parse(Settings *s, const char *text) {
    char msg[120];
    const char *p = text ? text : "";
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
        p += 3;
    int line_no = 0;
    while (*p) {
        const char *eol = strchr(p, '\n');
        int len = eol ? (int)(eol - p) : (int)strlen(p);
        char buf[160];
        if (len > (int)sizeof buf - 1) len = (int)sizeof buf - 1;
        memcpy(buf, p, (size_t)len);
        buf[len] = 0;
        p = eol ? eol + 1 : p + strlen(p);
        line_no++;
        char *hash = strchr(buf, '#');
        if (hash) *hash = 0;
        char *a = buf;
        while (*a == ' ' || *a == '\t' || *a == '\r' || (unsigned char)*a == 0xEF) a++;
        char *end = a + strlen(a);
        while (end > a && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) *--end = 0;
        if (!*a) continue;
        char *eq = strchr(a, '=');
        if (!eq) {
            snprintf(msg, sizeof msg, "settings.txt line %d has no '=' : %s", line_no, a);
            note(s, msg);
            continue;
        }
        *eq = 0;
        char *key = a;
        char *val = eq + 1;
        while (*key == ' ' || *key == '\t') key++;
        end = key + strlen(key);
        while (end > key && (end[-1] == ' ' || end[-1] == '\t')) *--end = 0;
        while (*val == ' ' || *val == '\t') val++;
        end = val + strlen(val);
        while (end > val && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) *--end = 0;
        for (char *k = key; *k; k++) *k = (char)toupper((unsigned char)*k);
        for (char *k = key; *k; k++) if (*k == ' ') *k = '_';
        char lv[64];
        snprintf(lv, sizeof lv, "%s", val);
        for (char *k = lv; *k; k++) *k = (char)tolower((unsigned char)*k);
        char *num_end = NULL;
        double num = strtod(val, &num_end);
        int bad = 0;
        const char *why = "bad value";
        if (!strcmp(key, "SHOW")) {
            if (!strcmp(lv, "text")) s->show = SHOW_TEXT;
            else if (!strcmp(lv, "image")) s->show = SHOW_IMAGE;
            else if (!strcmp(lv, "both")) s->show = SHOW_BOTH;
            else { bad = 1; why = "use text, image or both"; }
        } else if (!strcmp(key, "SECONDS")) {
            if (num_end == val) { bad = 1; why = "not a number"; }
            else s->seconds = num < 0.5 ? 0.5 : num;
        } else if (!strcmp(key, "STRETCH")) {
            if (num_end == val) { bad = 1; why = "not a number"; }
            else {
                if (num < 0.2) num = 0.2;
                if (num > 4.0) num = 4.0;
                s->stretch = num;
            }
        } else if (!strcmp(key, "LIT")) {
            if (!strcmp(lv, "dark")) s->lit_dark = 1;
            else if (!strcmp(lv, "light")) s->lit_dark = 0;
            else { bad = 1; why = "use dark or light"; }
        } else if (!strcmp(key, "PASSES")) {
            if (!strcmp(lv, "both")) s->passes = 0;
            else if (!strcmp(lv, "right")) s->passes = 1;
            else if (!strcmp(lv, "left")) s->passes = -1;
            else { bad = 1; why = "use both, right or left"; }
        } else if (!strcmp(key, "FLIP_VERTICAL")) {
            s->flip = yes(lv);
        } else if (!strcmp(key, "SIGN")) {
            if (num_end == val) { bad = 1; why = "not a number"; }
            else s->sign = num < 0 ? -1 : 1;
        } else if (!strcmp(key, "SHIFT_MS")) {
            if (num_end == val) { bad = 1; why = "not a number"; }
            else s->shift_ms = num;
        } else if (!strcmp(key, "CALIBRATE")) {
            s->calibrate = yes(lv);
        } else if (!strcmp(key, "ANCHOR")) {
            if (!strcmp(lv, "auto")) s->anchor = ANCHOR_AUTO;
            else if (!strcmp(lv, "zero")) s->anchor = ANCHOR_ZERO;
            else if (!strcmp(lv, "peaks")) s->anchor = ANCHOR_PEAKS;
            else { bad = 1; why = "use auto, zero or peaks"; }
        } else if (!strcmp(key, "SHAPE")) {
            if (!strcmp(lv, "auto")) s->shape = -1.0;
            else if (num_end == val) { bad = 1; why = "not a number"; }
            else {
                if (num < 0) num = 0;
                if (num > 1) num = 1;
                s->shape = num;
            }
        } else if (!strcmp(key, "WIDTH")) {
            char *pct = strchr(val, '%');
            if (pct) *pct = 0;
            num = strtod(val, &num_end);
            if (num_end == val) { bad = 1; why = "not a number"; }
            else {
                if (num < 20) num = 20;
                if (num > 95) num = 95;
                s->width = num;
            }
        } else if (!strcmp(key, "DEBUG")) {
            s->debug = yes(lv);
        } else if (!strcmp(key, "EDGE_FLASH")) {
            if (num_end == val) { bad = 1; why = "not a number"; }
            else {
                if (num < 0) num = 0;
                if (num > 0.2) num = 0.2;
                s->edge_flash = num;
            }
        } else if (!strcmp(key, "SLEEP")) {
            s->sleep_on = yes(lv);
        } else if (!strcmp(key, "SLEEP_S")) {
            if (num_end == val) { bad = 1; why = "not a number"; }
            else s->sleep_s = num < 1.0 ? 1.0 : num;
        } else if (!strcmp(key, "SLEEP_MODE")) {
            if (strcmp(lv, "light") && strcmp(lv, "deep") && strcmp(lv, "soft")) {
                bad = 1; why = "use light, deep or soft";
            } else {
                snprintf(s->sleep_mode, sizeof s->sleep_mode, "%s", lv);
            }
        } else if (!strcmp(key, "WAKE_G")) {
            if (num_end == val) { bad = 1; why = "not a number"; }
            else {
                if (num < 0.1) num = 0.1;
                if (num > 8.0) num = 8.0;
                s->wake_g = num;
            }
        } else if (!strcmp(key, "INT_TEST")) {
            s->int_test = yes(lv);
        } else {
            snprintf(msg, sizeof msg, "settings.txt line %d: unknown setting '%s'", line_no, key);
            note(s, msg);
            continue;
        }
        if (bad) {
            snprintf(msg, sizeof msg, "settings.txt line %d: could not use '%s' (%s)", line_no, a, why);
            note(s, msg);
        }
    }
}

/* 5x7 font, bit 0 = top row. Same glyphs as the v8 program. */
static int glyph(char ch, uint8_t out[5]) {
    static const struct { char ch; uint8_t c[5]; } font[] = {
        {' ', {0, 0, 0, 0, 0}},
        {'!', {0, 0, 0x5F, 0, 0}},
        {'-', {8, 8, 8, 8, 8}},
        {'.', {0, 0x60, 0x60, 0, 0}},
        {'?', {0x02, 0x01, 0x51, 0x09, 0x06}},
        {',', {0, 0x50, 0x30, 0, 0}},
        {':', {0, 0x36, 0x36, 0, 0}},
        {'\'', {0, 0x05, 0x03, 0, 0}},
        {'(', {0, 0x1C, 0x22, 0x41, 0}},
        {')', {0, 0x41, 0x22, 0x1C, 0}},
        {'+', {8, 8, 0x3E, 8, 8}},
        {'/', {0x20, 0x10, 8, 4, 2}},
        {'=', {0x14, 0x14, 0x14, 0x14, 0x14}},
        {'*', {0x14, 8, 0x3E, 8, 0x14}},
        {'_', {0x40, 0x40, 0x40, 0x40, 0x40}},
        {'~', {0x0C, 0x1E, 0x3C, 0x1E, 0x0C}},
        {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}}, {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
        {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}}, {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
        {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}}, {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
        {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}}, {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
        {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}}, {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
        {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}}, {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
        {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}}, {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
        {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}}, {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
        {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}}, {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
        {'S', {0x46, 0x49, 0x49, 0x49, 0x31}}, {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
        {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}}, {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
        {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}}, {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
        {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}}, {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
        {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}}, {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
        {'2', {0x42, 0x61, 0x51, 0x49, 0x46}}, {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
        {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}}, {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
        {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}}, {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
        {'8', {0x36, 0x49, 0x49, 0x49, 0x36}}, {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    };
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    for (unsigned i = 0; i < sizeof font / sizeof font[0]; i++) {
        if (font[i].ch == ch) {
            memcpy(out, font[i].c, 5);
            return 1;
        }
    }
    memcpy(out, font[0].c, 5);
    return 0;
}

static int line_columns(const char *txt, uint8_t *cols, int cap) {
    int n = 0;
    for (const char *p = txt; *p; p++) {
        uint8_t g[5];
        glyph(*p, g);
        for (int i = 0; i < 5; i++) {
            if (n < cap) cols[n++] = g[i];
        }
        if (n < cap) cols[n++] = 0;
    }
    if (n) n--;
    return n;
}

static uint16_t rev16(uint16_t m) {
    uint16_t r = 0;
    for (int i = 0; i < 16; i++) if (m & (1u << i)) r |= (uint16_t)(1u << (15 - i));
    return r;
}

static int build_text(Settings *s, const char *raw, uint16_t *masks, int cap) {
    char buf[1024];
    if (!raw) raw = "";
    snprintf(buf, sizeof buf, "%s", raw);
    if ((unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF)
        memmove(buf, buf + 3, strlen(buf + 3) + 1);
    for (char *p = buf; *p; p++) if (*p == '\r') *p = '\n';
    char lines[2][160];
    int nlines = 0;
    int extra = 0;
    char *save = buf;
    while (*save && nlines < 2) {
        char *nl = strchr(save, '\n');
        if (nl) *nl = 0;
        char *a = save;
        while (*a == ' ' || *a == '\t') a++;
        char *e = a + strlen(a);
        while (e > a && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
        if (*a) {
            snprintf(lines[nlines], sizeof lines[nlines], "%s", a);
            nlines++;
        }
        if (!nl) break;
        save = nl + 1;
        if (nlines == 2 && *save) {
            /* more non-empty lines? */
            char *q = save;
            while (*q) {
                if (*q != '\n' && *q != ' ' && *q != '\t') { extra = 1; break; }
                q++;
            }
            break;
        }
    }
    if (extra) {
        char msg[80];
        snprintf(msg, sizeof msg, "message.txt has extra lines; only the first 2 are shown");
        note(s, msg);
    }
    if (!nlines) snprintf(lines[nlines++], 16, "HELLO");

    uint8_t colb[2][COL_CAP];
    int cn[2] = {0, 0};
    int width = 0;
    for (int i = 0; i < nlines; i++) {
        cn[i] = line_columns(lines[i], colb[i], COL_CAP);
        if (cn[i] > width) width = cn[i];
    }
    if (width > cap) width = cap;
    if (width < 1) width = 1;
    for (int i = 0; i < width; i++) masks[i] = 0;
    for (int li = 0; li < nlines; li++) {
        int off = (width - cn[li]) / 2;
        if (off < 0) off = 0;
        for (int i = 0; i < cn[li] && off + i < width; i++) {
            uint16_t m = 0;
            uint8_t b = colb[li][i];
            for (int r = 0; r < 7; r++) {
                if ((b >> r) & 1) {
                    if (nlines == 1) m |= (uint16_t)(3u << (13 - 2 * r));
                    else if (li == 0) m |= (uint16_t)(1u << (15 - r));
                    else m |= (uint16_t)(1u << (6 - r));
                }
            }
            masks[off + i] |= m;
        }
    }
    return width;
}

static int rd_u16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static int rd_u32(const uint8_t *p) { return (int)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); }
static int rd_s32(const uint8_t *p) { return rd_u32(p); }

static int load_bmp(Settings *s, const uint8_t *bmp, int len, uint16_t *cols, int cap) {
    char msg[96];
    if (!bmp || len < 54 || bmp[0] != 'B' || bmp[1] != 'M') {
        note(s, "image.bmp: not a .bmp file");
        return -1;
    }
    int data_off = rd_u32(bmp + 10);
    int dib = rd_u32(bmp + 14);
    int w = rd_s32(bmp + 18);
    int h = rd_s32(bmp + 22);
    int bpp = rd_u16(bmp + 28);
    int comp = rd_u32(bmp + 30);
    int ncol = rd_u32(bmp + 46);
    if ((comp != 0 && comp != 3) || (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32)) {
        note(s, "image.bmp: unsupported bmp type (save as Monochrome or 24-bit Bitmap)");
        return -1;
    }
    int top_down = h < 0;
    if (h < 0) h = -h;
    if (w < 1 || h < 1) {
        note(s, "image.bmp: empty picture");
        return -1;
    }
    int pal[256];
    int npal = 0;
    if (bpp <= 8) {
        npal = ncol ? ncol : (1 << bpp);
        if (npal > 256) npal = 256;
        int pal_at = 14 + dib;
        if (pal_at < 0 || pal_at + npal * 4 > len) {
            note(s, "image.bmp: palette is cut short");
            return -1;
        }
        for (int i = 0; i < npal; i++) {
            int b = bmp[pal_at + 4 * i];
            int g = bmp[pal_at + 4 * i + 1];
            int r = bmp[pal_at + 4 * i + 2];
            pal[i] = (r * 30 + g * 59 + b * 11) / 100;
        }
    }
    int row_bytes = ((w * bpp + 31) / 32) * 4;
    if (row_bytes <= 0 || row_bytes > 8192) {
        note(s, "image.bmp: picture is too wide");
        return -1;
    }
    int out_h = 16;
    int out_w = (h != out_h) ? (w * out_h + h / 2) / h : w;
    if (out_w < 1) out_w = 1;
    if (out_w > cap) out_w = cap;
    uint8_t row[8192];
    for (int c = 0; c < out_w; c++) cols[c] = 0;
    for (int r = 0; r < out_h; r++) {
        int sy = (int)((r + 0.5) * h / out_h);
        if (sy > h - 1) sy = h - 1;
        int fy = top_down ? sy : (h - 1 - sy);
        int off = data_off + fy * row_bytes;
        if (off < 0 || off + row_bytes > len) {
            snprintf(msg, sizeof msg, "image.bmp: picture data is cut short");
            note(s, msg);
            return -1;
        }
        memcpy(row, bmp + off, (size_t)row_bytes);
        for (int c = 0; c < out_w; c++) {
            int x = (int)((c + 0.5) * w / out_w);
            if (x > w - 1) x = w - 1;
            int lum;
            if (bpp == 1) {
                int bit = (row[x >> 3] >> (7 - (x & 7))) & 1;
                lum = npal ? pal[bit] : (bit ? 255 : 0);
            } else if (bpp == 4) {
                int v = row[x >> 1];
                int ix = (x & 1) ? (v & 15) : (v >> 4);
                if (ix >= npal) ix = 0;
                lum = pal[ix];
            } else if (bpp == 8) {
                int ix = row[x];
                if (ix >= npal) ix = 0;
                lum = pal[ix];
            } else {
                int o = x * (bpp >> 3);
                lum = (row[o + 2] * 30 + row[o + 1] * 59 + row[o] * 11) / 100;
            }
            int on = s->lit_dark ? (lum < 128) : (lum >= 128);
            if (on) cols[c] |= (uint16_t)(1u << (15 - r));
        }
    }
    return out_w;
}

static void finish_frame(const Settings *s, uint16_t *masks, int n, double pitch, double *half_out) {
    if (s->flip) {
        for (int i = 0; i < n; i++) masks[i] = rev16(masks[i]);
    }
    double half = n * pitch * s->stretch / 2.0;
    double limit = s->width / 100.0;
    if (half > limit) half = limit;
    *half_out = half;
}

void frames_build(Settings *s, const char *message, const uint8_t *bmp, int bmp_len, Frames *out) {
    memset(out, 0, sizeof *out);
    if (s->show == SHOW_TEXT || s->show == SHOW_BOTH) {
        int n = build_text(s, message && message[0] ? message : NULL, out->cols[out->count], COL_CAP);
        if (!message || !message[0]) note(s, "message.txt not found - showing HELLO");
        finish_frame(s, out->cols[out->count], n, PITCH_TEXT, &out->half[out->count]);
        out->n[out->count] = n;
        memcpy(out->name[out->count], "text", 5);
        out->count++;
    }
    if (s->show == SHOW_IMAGE || s->show == SHOW_BOTH) {
        int n = load_bmp(s, bmp, bmp_len, out->cols[out->count], COL_CAP);
        if (n > 0) {
            finish_frame(s, out->cols[out->count], n, PITCH_IMG, &out->half[out->count]);
            out->n[out->count] = n;
            memcpy(out->name[out->count], "image", 6);
            out->count++;
        }
    }
    if (!out->count) {
        int n = build_text(s, "HELLO", out->cols[0], COL_CAP);
        finish_frame(s, out->cols[0], n, PITCH_TEXT, &out->half[0]);
        out->n[0] = n;
        memcpy(out->name[0], "text", 5);
        out->count = 1;
    }
}
