/* linenoise_win.c -- Windows port of linenoise library.
 *
 * This file is a standalone Windows implementation of the linenoise API
 * defined in linenoise.h. It uses the Win32 Console API and Virtual
 * Terminal Processing to support ANSI escape sequences.
 *
 * ------------------------------------------------------------------------
 *
 * Copyright (c) 2026, Lorenzo Tumini (lorenzotumini)
 * Derived from linenoise.c:
 * Copyright (c) 2010-2023, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2013, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  *  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  *  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <windows.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include "linenoise.h"

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#if defined(_MSC_VER) && _MSC_VER < 1900
#define snprintf _snprintf
#endif

#define strcasecmp _stricmp

#define LINENOISE_DEFAULT_HISTORY_MAX_LEN 100
#define LINENOISE_MAX_LINE 4096
#define LINENOISE_DEFAULT_COLUMNS 80

/* Terminal refresh flags */
#define REFRESH_CLEAN (1<<0)    /* Clean the old prompt from the screen */
#define REFRESH_WRITE (1<<1)    /* Write the prompt and the current buffer */
#define REFRESH_ALL (REFRESH_CLEAN|REFRESH_WRITE)

static char *unsupported_term[] = {"dumb","cons25","emacs",NULL};
static linenoiseCompletionCallback *completionCallback = NULL;
static linenoiseHintsCallback *hintsCallback = NULL;
static linenoiseFreeHintsCallback *freeHintsCallback = NULL;

static DWORD orig_console_in_mode;
static DWORD orig_console_out_mode;
static UINT orig_console_output_cp;
static WCHAR pending_high_surrogate;
static int rawmode = 0;
static int mlmode = 0;
static int maskmode = 0;
static int atexit_registered = 0;
static int history_max_len = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
static int history_len = 0;
static char **history = NULL;

/* Forward declarations */
static void refreshLine(struct linenoiseState *l);
static void refreshLineWithFlags(struct linenoiseState *l, int flags);
static void linenoiseAtExit(void);
static char *linenoiseNoTTY(void);

/* =========================== Win32 Helpers ================================ */

static int isUnsupportedTerm(void) {
    char *term = getenv("TERM");
    int j;

    if (term == NULL) return 0;
    for (j = 0; unsupported_term[j]; j++)
        if (!strcasecmp(term,unsupported_term[j])) return 1;
    return 0;
}

static int enableRawMode(int fd) {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwInMode = 0;
    DWORD dwOutMode = 0;

    if (getenv("LINENOISE_ASSUME_TTY")) {
        rawmode = 1;
        return 0;
    }

    if (!_isatty(fd)) goto fatal;
    if (!atexit_registered) {
        atexit(linenoiseAtExit);
        atexit_registered = 1;
    }

    if (!GetConsoleMode(hIn, &orig_console_in_mode)) goto fatal;
    if (!GetConsoleMode(hOut, &orig_console_out_mode)) goto fatal;

    dwInMode = orig_console_in_mode;
    dwInMode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT |
                  ENABLE_PROCESSED_INPUT | ENABLE_VIRTUAL_TERMINAL_INPUT);
    dwInMode |= ENABLE_WINDOW_INPUT;

    dwOutMode = orig_console_out_mode;
    dwOutMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;

    if (!SetConsoleMode(hIn, dwInMode)) goto fatal;
    if (!SetConsoleMode(hOut, dwOutMode)) {
        /* Fallback if VT processing is not supported */
        SetConsoleMode(hIn, orig_console_in_mode);
        goto fatal;
    }
    orig_console_output_cp = GetConsoleOutputCP();
    if (orig_console_output_cp == 0 || !SetConsoleOutputCP(CP_UTF8)) {
        SetConsoleMode(hIn, orig_console_in_mode);
        SetConsoleMode(hOut, orig_console_out_mode);
        goto fatal;
    }

    rawmode = 1;
    return 0;

fatal:
    errno = ENOTTY;
    return -1;
}

static void disableRawMode(int fd) {
    (void)fd;
    if (getenv("LINENOISE_ASSUME_TTY")) {
        rawmode = 0;
        return;
    }
    if (rawmode) {
        SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), orig_console_in_mode);
        SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), orig_console_out_mode);
        if (orig_console_output_cp != 0)
            SetConsoleOutputCP(orig_console_output_cp);
        rawmode = 0;
    }
}

static int getColumns(int ifd, int ofd) {
    (void)ifd; (void)ofd;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    char *cols_env = getenv("LINENOISE_COLS");
    if (cols_env) {
        char *end = NULL;
        int saved_errno = errno;
        long cols;

        errno = 0;
        cols = strtol(cols_env,&end,10);
        if (errno != 0 || end == cols_env || *end != '\0' ||
            cols <= 0 || cols > INT_MAX)
        {
            cols = LINENOISE_DEFAULT_COLUMNS;
        }
        errno = saved_errno;
        return (int)cols;
    }

    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        return csbi.srWindow.Right - csbi.srWindow.Left + 1;
    }
    return LINENOISE_DEFAULT_COLUMNS;
}

static void updateColumns(struct linenoiseState *l) {
    int cols = getColumns(l->ifd,l->ofd);
    l->cols = cols > 0 ? (size_t)cols : LINENOISE_DEFAULT_COLUMNS;
}

/* =========================== UTF-8 support ================================ */

static int utf8EncodeChar(uint32_t cp, char out[4]) {
    if (cp <= 0x7f) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp <= 0x7ff) {
        out[0] = (char)(0xc0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3f));
        return 2;
    }
    if ((cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff) cp = 0xfffd;
    if (cp <= 0xffff) {
        out[0] = (char)(0xe0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
        out[2] = (char)(0x80 | (cp & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
    out[3] = (char)(0x80 | (cp & 0x3f));
    return 4;
}

static uint32_t utf8DecodeChar(const char *s, size_t *len) {
    unsigned char *p = (unsigned char *)s;
    uint32_t cp;
    if ((*p & 0x80) == 0) {
        *len = 1;
        return *p;
    } else if ((*p & 0xE0) == 0xC0) {
        *len = 2;
        cp = (*p & 0x1F) << 6;
        cp |= (p[1] & 0x3F);
        return cp;
    } else if ((*p & 0xF0) == 0xE0) {
        *len = 3;
        cp = (*p & 0x0F) << 12;
        cp |= (p[1] & 0x3F) << 6;
        cp |= (p[2] & 0x3F);
        return cp;
    } else if ((*p & 0xF8) == 0xF0) {
        *len = 4;
        cp = (*p & 0x07) << 18;
        cp |= (p[1] & 0x3F) << 12;
        cp |= (p[2] & 0x3F) << 6;
        cp |= (p[3] & 0x3F);
        return cp;
    }
    *len = 1;
    return *p;
}

static int isVariationSelector(uint32_t cp) { return cp == 0xFE0E || cp == 0xFE0F; }
static int isSkinToneModifier(uint32_t cp) { return cp >= 0x1F3FB && cp <= 0x1F3FF; }
static int isZWJ(uint32_t cp) { return cp == 0x200D; }
static int isRegionalIndicator(uint32_t cp) { return cp >= 0x1F1E6 && cp <= 0x1F1FF; }
static int isCombiningMark(uint32_t cp) {
    return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
           (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
           (cp >= 0xFE20 && cp <= 0xFE2F);
}
static int isGraphemeExtend(uint32_t cp) {
    return isVariationSelector(cp) || isSkinToneModifier(cp) || isZWJ(cp) || isCombiningMark(cp);
}

static uint32_t utf8DecodePrev(const char *buf, size_t pos, size_t *cplen) {
    if (pos == 0) { *cplen = 0; return 0; }
    size_t i = pos;
    do { i--; } while (i > 0 && (pos - i) < 4 && ((unsigned char)buf[i] & 0xC0) == 0x80);
    *cplen = pos - i;
    size_t dummy;
    return utf8DecodeChar(buf + i, &dummy);
}

static size_t utf8PrevCharLen(const char *buf, size_t pos) {
    if (pos == 0) return 0;
    size_t total = 0, curpos = pos, cplen;
    uint32_t cp = utf8DecodePrev(buf, curpos, &cplen);
    if (cplen == 0) return 0;
    total += cplen; curpos -= cplen;
    while (curpos > 0) {
        size_t prevlen;
        uint32_t prevcp = utf8DecodePrev(buf, curpos, &prevlen);
        if (prevlen == 0) break;
        if (isZWJ(prevcp)) {
            total += prevlen; curpos -= prevlen;
            prevcp = utf8DecodePrev(buf, curpos, &prevlen);
            if (prevlen == 0) break;
            total += prevlen; curpos -= prevlen; cp = prevcp; continue;
        } else if (isGraphemeExtend(cp)) {
            total += prevlen; curpos -= prevlen; cp = prevcp; continue;
        } else if (isRegionalIndicator(cp) && isRegionalIndicator(prevcp)) {
            total += prevlen; curpos -= prevlen; break;
        } else break;
    }
    return total;
}

static size_t utf8NextCharLen(const char *buf, size_t pos, size_t len) {
    if (pos >= len) return 0;
    size_t total = 0, curpos = pos, cplen;
    uint32_t cp = utf8DecodeChar(buf + curpos, &cplen);
    total += cplen; curpos += cplen;
    int isRI = isRegionalIndicator(cp);
    while (curpos < len) {
        size_t nextlen;
        uint32_t nextcp = utf8DecodeChar(buf + curpos, &nextlen);
        if (isZWJ(nextcp) && curpos + nextlen < len) {
            total += nextlen; curpos += nextlen;
            nextcp = utf8DecodeChar(buf + curpos, &nextlen);
            total += nextlen; curpos += nextlen; continue;
        } else if (isGraphemeExtend(nextcp)) {
            total += nextlen; curpos += nextlen; continue;
        } else if (isRI && isRegionalIndicator(nextcp)) {
            total += nextlen; curpos += nextlen; isRI = 0; continue;
        } else break;
    }
    return total;
}

static int utf8CharWidth(uint32_t cp) {
    if (cp < 32 || (cp >= 0x7F && cp < 0xA0)) return 0;
    if (isCombiningMark(cp)) return 0;
    if (isVariationSelector(cp)) return 0;
    if (isSkinToneModifier(cp)) return 0;
    if (isZWJ(cp)) return 0;
    if (cp >= 0x1100 &&
        (cp <= 0x115F || cp == 0x2329 || cp == 0x232A || (cp >= 0x231A && cp <= 0x231B) ||
         (cp >= 0x23E9 && cp <= 0x23F3) || (cp >= 0x23F8 && cp <= 0x23FA) ||
         (cp >= 0x25AA && cp <= 0x25AB) || (cp >= 0x25B6 && cp <= 0x25C0) ||
         (cp >= 0x25FB && cp <= 0x25FE) || (cp >= 0x2600 && cp <= 0x26FF) ||
         (cp >= 0x2700 && cp <= 0x27BF) || (cp >= 0x2934 && cp <= 0x2935) ||
         (cp >= 0x2B05 && cp <= 0x2B07) || (cp >= 0x2B1B && cp <= 0x2B1C) ||
         cp == 0x2B50 || cp == 0x2B55 || (cp >= 0x2E80 && cp <= 0xA4CF && cp != 0x303F) ||
         (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
         (cp >= 0xFE10 && cp <= 0xFE1F) || (cp >= 0xFE30 && cp <= 0xFE6F) ||
         (cp >= 0xFF00 && cp <= 0xFF60) || (cp >= 0xFFE0 && cp <= 0xFFE6) ||
         (cp >= 0x1F1E6 && cp <= 0x1F1FF) || (cp >= 0x1F300 && cp <= 0x1F64F) ||
         (cp >= 0x1F680 && cp <= 0x1F6FF) || (cp >= 0x1F900 && cp <= 0x1F9FF) ||
         (cp >= 0x1FA00 && cp <= 0x1FAFF) || (cp >= 0x20000 && cp <= 0x2FFFF)))
        return 2;
    return 1;
}

static size_t ansiEscapeLen(const char *s, size_t len) {
    size_t i;
    if (len < 2 || s[1] != '[') return 0;
    i = 2;
    while (i < len && (unsigned char)s[i] >= 0x30 && (unsigned char)s[i] <= 0x3f) i++;
    while (i < len && (unsigned char)s[i] >= 0x20 && (unsigned char)s[i] <= 0x2f) i++;
    if (i >= len || (unsigned char)s[i] < 0x40 || (unsigned char)s[i] > 0x7e) return 0;
    return i + 1;
}

static size_t utf8StrWidth(const char *s, size_t len) {
    size_t width = 0, i = 0;
    int after_zwj = 0;
    while (i < len) {
        size_t clen;
        uint32_t cp = utf8DecodeChar(s + i, &clen);
        if (cp == 0x1b) {
            size_t skip = ansiEscapeLen(s + i, len - i);
            if (skip > 0) { i += skip; continue; }
        }
        if (after_zwj) after_zwj = 0;
        else width += utf8CharWidth(cp);
        if (isZWJ(cp)) after_zwj = 1;
        i += clen;
    }
    return width;
}

static int utf8SingleCharWidth(const char *s, size_t len) {
    if (len == 0) return 0;
    size_t clen;
    uint32_t cp = utf8DecodeChar(s, &clen);
    return utf8CharWidth(cp);
}

/* ============================== Completion ================================ */

static void freeCompletions(linenoiseCompletions *lc) {
    size_t i;
    for (i = 0; i < lc->len; i++) free(lc->cvec[i]);
    if (lc->cvec != NULL) free(lc->cvec);
}

static void refreshLineWithCompletion(struct linenoiseState *ls, linenoiseCompletions *lc, int flags) {
    linenoiseCompletions ctable = { 0, NULL };
    if (lc == NULL) {
        completionCallback(ls->buf,&ctable);
        lc = &ctable;
    }
    if (ls->completion_idx < lc->len) {
        struct linenoiseState saved = *ls;
        ls->len = ls->pos = strlen(lc->cvec[ls->completion_idx]);
        ls->buf = lc->cvec[ls->completion_idx];
        refreshLineWithFlags(ls,flags);
        ls->len = saved.len; ls->pos = saved.pos; ls->buf = saved.buf;
    } else {
        refreshLineWithFlags(ls,flags);
    }
    if (lc == &ctable) freeCompletions(&ctable);
}

static void acceptCompletion(struct linenoiseState *ls,
                             const char *completion) {
    size_t completion_len = strlen(completion);
    size_t copy_len = completion_len;

    if (copy_len > ls->buflen) {
        copy_len = ls->buflen;
        while (copy_len > 0 &&
               ((unsigned char)completion[copy_len] & 0xc0) == 0x80)
        {
            copy_len--;
        }
    }
    memcpy(ls->buf,completion,copy_len);
    ls->buf[copy_len] = '\0';
    ls->len = ls->pos = copy_len;
}

static void acceptDisplayedCompletion(struct linenoiseState *ls) {
    linenoiseCompletions lc = { 0, NULL };

    if (!ls->in_completion || completionCallback == NULL) return;
    completionCallback(ls->buf,&lc);
    if (ls->completion_idx < lc.len)
        acceptCompletion(ls,lc.cvec[ls->completion_idx]);
    ls->in_completion = 0;
    freeCompletions(&lc);
    refreshLine(ls);
}

static int completeLine(struct linenoiseState *ls, int keypressed) {
    linenoiseCompletions lc = { 0, NULL };
    char c = keypressed;
    completionCallback(ls->buf,&lc);
    if (lc.len == 0) {
        MessageBeep(MB_OK);
        ls->in_completion = 0;
        c = 0;
    } else {
        switch(c) {
            case 9:
                if (ls->in_completion == 0) {
                    ls->in_completion = 1;
                    ls->completion_idx = 0;
                } else {
                    ls->completion_idx = (ls->completion_idx+1) % (lc.len+1);
                    if (ls->completion_idx == lc.len) MessageBeep(MB_OK);
                }
                c = 0;
                break;
            case 27:
                if (ls->completion_idx < lc.len) refreshLine(ls);
                ls->in_completion = 0;
                c = 0;
                break;
            default:
                if (ls->completion_idx < lc.len) {
                    acceptCompletion(ls,lc.cvec[ls->completion_idx]);
                }
                ls->in_completion = 0;
                break;
        }
        if (ls->in_completion && ls->completion_idx < lc.len) {
            refreshLineWithCompletion(ls,&lc,REFRESH_ALL);
        } else {
            refreshLine(ls);
        }
    }
    freeCompletions(&lc);
    return c;
}

void linenoiseSetCompletionCallback(linenoiseCompletionCallback *fn) { completionCallback = fn; }
void linenoiseSetHintsCallback(linenoiseHintsCallback *fn) { hintsCallback = fn; }
void linenoiseSetFreeHintsCallback(linenoiseFreeHintsCallback *fn) { freeHintsCallback = fn; }
void linenoiseAddCompletion(linenoiseCompletions *lc, const char *str) {
    size_t len = strlen(str);
    char *copy, **cvec;
    copy = malloc(len+1);
    if (copy == NULL) return;
    memcpy(copy,str,len+1);
    cvec = realloc(lc->cvec,sizeof(char*)*(lc->len+1));
    if (cvec == NULL) { free(copy); return; }
    lc->cvec = cvec;
    lc->cvec[lc->len++] = copy;
}

/* =========================== Line editing ================================= */

struct abuf { char *b; int len; };
static void abInit(struct abuf *ab) { ab->b = NULL; ab->len = 0; }
static int abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);
    if (new == NULL) return -1; /* Signal failure */
    memcpy(new + ab->len, s, len);
    ab->b = new;
    ab->len += len;
    return 0;
}
static void abFree(struct abuf *ab) { free(ab->b); }

void refreshShowHints(struct abuf *ab, struct linenoiseState *l, int pwidth) {
    char seq[64];
    size_t bufwidth = utf8StrWidth(l->buf, l->len);
    if (hintsCallback && pwidth + bufwidth < l->cols) {
        int color = -1, bold = 0;
        char *hint = hintsCallback(l->buf,&color,&bold);
        if (hint) {
            size_t hintlen = strlen(hint);
            size_t hintwidth = utf8StrWidth(hint, hintlen);
            size_t hintmaxwidth = l->cols - (pwidth + bufwidth);
            if (hintwidth > hintmaxwidth) {
                size_t i = 0, w = 0;
                while (i < hintlen) {
                    size_t clen = utf8NextCharLen(hint, i, hintlen);
                    int cwidth = utf8SingleCharWidth(hint + i, clen);
                    if (w + cwidth > hintmaxwidth) break;
                    w += cwidth; i += clen;
                }
                hintlen = i;
            }
            if (bold == 1 && color == -1) color = 37;
            if (color != -1 || bold != 0) snprintf(seq,64,"\033[%d;%d;49m",bold,color);
            else seq[0] = '\0';
            abAppend(ab,seq,strlen(seq));
            abAppend(ab,hint,hintlen);
            if (color != -1 || bold != 0) abAppend(ab,"\033[0m",4);
            if (freeHintsCallback) freeHintsCallback(hint);
        }
    }
}

static void refreshSingleLine(struct linenoiseState *l, int flags) {
    char seq[64];
    size_t pwidth = utf8StrWidth(l->prompt, l->plen);
    int fd = l->ofd;
    char *buf = l->buf;
    size_t len = l->len, pos = l->pos, poscol, lencol;
    struct abuf ab;
    poscol = utf8StrWidth(buf, pos);
    lencol = utf8StrWidth(buf, len);
    while (pos > 0 && pwidth + poscol >= l->cols) {
        size_t clen = utf8NextCharLen(buf, 0, len);
        int cwidth = utf8SingleCharWidth(buf, clen);
        if (clen == 0 || clen > pos) break;
        buf += clen; len -= clen; pos -= clen; poscol -= cwidth; lencol -= cwidth;
    }
    while (len > 0 && pwidth + lencol > l->cols) {
        size_t clen = utf8PrevCharLen(buf, len);
        int cwidth = utf8SingleCharWidth(buf + len - clen, clen);
        if (clen == 0) break;
        len -= clen; lencol -= cwidth;
    }
    abInit(&ab);
    snprintf(seq,sizeof(seq),"\r");
    abAppend(&ab,seq,strlen(seq));
    if (flags & REFRESH_WRITE) {
        abAppend(&ab,l->prompt,l->plen);
        if (maskmode == 1) {
            size_t i = 0;
            while (i < len) { abAppend(&ab,"*",1); i += utf8NextCharLen(buf, i, len); }
        } else abAppend(&ab,buf,len);
        refreshShowHints(&ab,l,pwidth);
    }
    snprintf(seq,sizeof(seq),"\x1b[0K");
    abAppend(&ab,seq,strlen(seq));
    if (flags & REFRESH_WRITE) {
        snprintf(seq,sizeof(seq),"\r\x1b[%dC", (int)(poscol+pwidth));
        abAppend(&ab,seq,strlen(seq));
    }
    _write(fd,ab.b,ab.len);
    abFree(&ab);
}

static void refreshMultiLine(struct linenoiseState *l, int flags) {
    char seq[64];
    size_t pwidth = utf8StrWidth(l->prompt, l->plen);
    size_t bufwidth = utf8StrWidth(l->buf, l->len);
    size_t poswidth = utf8StrWidth(l->buf, l->pos);
    int rows = (pwidth+bufwidth+l->cols-1)/l->cols;
    int rpos = l->oldrpos, rpos2, col, old_rows = l->oldrows;
    int fd = l->ofd, j;
    struct abuf ab;
    l->oldrows = rows;
    abInit(&ab);
    if (flags & REFRESH_CLEAN) {
        if (old_rows-rpos > 0) {
            snprintf(seq,64,"\x1b[%dB", old_rows-rpos);
            abAppend(&ab,seq,strlen(seq));
        }
        for (j = 0; j < old_rows-1; j++) {
            snprintf(seq,64,"\r\x1b[0K\x1b[1A");
            abAppend(&ab,seq,strlen(seq));
        }
    }
    if (flags & REFRESH_ALL) { snprintf(seq,64,"\r\x1b[0K"); abAppend(&ab,seq,strlen(seq)); }
    if (flags & REFRESH_WRITE) {
        abAppend(&ab,l->prompt,l->plen);
        if (maskmode == 1) {
            size_t i = 0;
            while (i < l->len) { abAppend(&ab,"*",1); i += utf8NextCharLen(l->buf, i, l->len); }
        } else abAppend(&ab,l->buf,l->len);
        refreshShowHints(&ab,l,pwidth);
        if (l->pos && l->pos == l->len && (poswidth+pwidth) % l->cols == 0) {
            abAppend(&ab,"\n",1); snprintf(seq,64,"\r"); abAppend(&ab,seq,strlen(seq));
            rows++; if (rows > (int)l->oldrows) l->oldrows = rows;
        }
        rpos2 = (pwidth+poswidth+l->cols)/l->cols;
        if (rows-rpos2 > 0) {
            snprintf(seq,64,"\x1b[%dA", rows-rpos2);
            abAppend(&ab,seq,strlen(seq));
        }
        col = (pwidth+poswidth) % l->cols;
        if (col) snprintf(seq,64,"\r\x1b[%dC", col);
        else snprintf(seq,64,"\r");
        abAppend(&ab,seq,strlen(seq));
    }
    l->oldpos = l->pos;
    if (flags & REFRESH_WRITE) l->oldrpos = rpos2;
    _write(fd,ab.b,ab.len);
    abFree(&ab);
}

static void refreshLineWithFlags(struct linenoiseState *l, int flags) {
    updateColumns(l);
    if (mlmode) refreshMultiLine(l,flags);
    else refreshSingleLine(l,flags);
}

static void refreshLine(struct linenoiseState *l) { refreshLineWithFlags(l,REFRESH_ALL); }

void linenoiseHide(struct linenoiseState *l) {
    refreshLineWithFlags(l,REFRESH_CLEAN);
}

void linenoiseShow(struct linenoiseState *l) {
    if (l->in_completion) refreshLineWithCompletion(l,NULL,REFRESH_WRITE);
    else refreshLineWithFlags(l,REFRESH_WRITE);
}

/* On error writing to the terminal -1 is returned, otherwise 0. */
int linenoiseEditInsert(struct linenoiseState *l, const char *c, size_t clen) {
    if (l->len + clen <= l->buflen) {
        if (l->len == l->pos) {
            memcpy(l->buf+l->pos, c, clen);
            l->pos += clen; l->len += clen; l->buf[l->len] = '\0';
            if ((!mlmode && utf8StrWidth(l->prompt,l->plen)+utf8StrWidth(l->buf,l->len) < l->cols && !hintsCallback)) {
                if (maskmode == 1) {
                    if (_write(l->ofd,"*",1) != 1) return -1;
                } else if (_write(l->ofd,c,(unsigned int)clen) != (int)clen) {
                    return -1;
                }
            } else refreshLine(l);
        } else {
            memmove(l->buf+l->pos+clen, l->buf+l->pos, l->len-l->pos);
            memcpy(l->buf+l->pos, c, clen);
            l->len += clen; l->pos += clen; l->buf[l->len] = '\0';
            refreshLine(l);
        }
    }
    return 0;
}

void linenoiseEditMoveLeft(struct linenoiseState *l) {
    if (l->pos > 0) { l->pos -= utf8PrevCharLen(l->buf, l->pos); refreshLine(l); }
}
void linenoiseEditMoveRight(struct linenoiseState *l) {
    if (l->pos != l->len) { l->pos += utf8NextCharLen(l->buf, l->pos, l->len); refreshLine(l); }
}
void linenoiseEditMoveHome(struct linenoiseState *l) { if (l->pos != 0) { l->pos = 0; refreshLine(l); } }
void linenoiseEditMoveEnd(struct linenoiseState *l) { if (l->pos != l->len) { l->pos = l->len; refreshLine(l); } }

void linenoiseEditHistoryNext(struct linenoiseState *l, int dir) {
    if (l->history_temp_active && history_len > 1) {
        int current_index = history_len - 1 - l->history_index;
        char *copy = _strdup(l->buf);
        if (copy == NULL) return;
        free(history[current_index]);
        history[current_index] = copy;
        l->history_index += (dir == 1) ? 1 : -1;
        if (l->history_index < 0) { l->history_index = 0; return; }
        else if (l->history_index >= history_len) { l->history_index = history_len-1; return; }
        strncpy(l->buf,history[history_len - 1 - l->history_index],l->buflen);
        l->buf[l->buflen] = '\0';
        l->len = l->pos = strlen(l->buf);
        refreshLine(l);
    }
}

static void discardHistoryPlaceholder(struct linenoiseState *l) {
    if (!l->history_temp_active) return;
    if (history_len > 0) {
        history_len--;
        free(history[history_len]);
        history[history_len] = NULL;
    }
    l->history_temp_active = 0;
}

static void addHistoryPlaceholder(struct linenoiseState *l) {
    if (linenoiseHistoryAdd("")) {
        l->history_temp_active = 1;
    } else if (history_len > 0 && history[history_len-1] != NULL &&
               history[history_len-1][0] == '\0')
    {
        l->history_temp_active = 1;
    }
}

void linenoiseEditDelete(struct linenoiseState *l) {
    if (l->len > 0 && l->pos < l->len) {
        size_t clen = utf8NextCharLen(l->buf, l->pos, l->len);
        memmove(l->buf+l->pos, l->buf+l->pos+clen, l->len-l->pos-clen);
        l->len -= clen; l->buf[l->len] = '\0'; refreshLine(l);
    }
}
void linenoiseEditBackspace(struct linenoiseState *l) {
    if (l->pos > 0 && l->len > 0) {
        size_t clen = utf8PrevCharLen(l->buf, l->pos);
        memmove(l->buf+l->pos-clen, l->buf+l->pos, l->len-l->pos);
        l->pos -= clen; l->len -= clen; l->buf[l->len] = '\0'; refreshLine(l);
    }
}
void linenoiseEditDeletePrevWord(struct linenoiseState *l) {
    size_t old_pos = l->pos, diff;

    /* Skip trailing spaces (compare decoded codepoint, not raw byte) */
    while (l->pos > 0) {
        size_t cplen;
        uint32_t cp = utf8DecodePrev(l->buf, l->pos, &cplen);
        if (cp != ' ') break;  /* U+0020 only; extend if you want U+3000 etc. */
        l->pos -= cplen;
    }
    /* Skip the preceding word */
    while (l->pos > 0) {
        size_t cplen;
        uint32_t cp = utf8DecodePrev(l->buf, l->pos, &cplen);
        if (cp == ' ') break;
        l->pos -= cplen;
    }

    diff = old_pos - l->pos;
    memmove(l->buf + l->pos, l->buf + old_pos, l->len - old_pos + 1);
    l->len -= diff;
    refreshLine(l);
}

int linenoiseEditStart(struct linenoiseState *l, int stdin_fd, int stdout_fd,
                       char *buf, size_t buflen, const char *prompt) {
    if (l == NULL || buf == NULL || buflen == 0 || prompt == NULL) {
        errno = EINVAL;
        return -1;
    }
    l->in_completion = 0;
    l->ifd = stdin_fd != -1 ? stdin_fd : STDIN_FILENO;
    l->ofd = stdout_fd != -1 ? stdout_fd : STDOUT_FILENO;
    l->buf = buf; l->buflen = buflen; l->prompt = prompt; l->plen = strlen(prompt);
    l->oldpos = l->pos = 0; l->len = 0;
    l->history_temp_active = 0;
    pending_high_surrogate = 0;
    /* Always initialize these so linenoiseEditFeed sees valid state
     * even when called on a non-TTY fd. */
    l->cols = 80;
    l->oldrows = 0; l->oldrpos = 1; l->history_index = 0;
    l->buf[0] = '\0'; l->buflen--;

    if (enableRawMode(l->ifd) == -1) return -1;
    l->cols = getColumns(l->ifd,l->ofd); /* overwrite with real value */

    if (!_isatty(l->ifd) && !getenv("LINENOISE_ASSUME_TTY")) return 0;
    addHistoryPlaceholder(l);
    if (_write(l->ofd,prompt,(unsigned int)l->plen) != (int)l->plen) {
        discardHistoryPlaceholder(l);
        disableRawMode(l->ifd);
        return -1;
    }
    return 0;
}

char *linenoiseEditMore = "If you see this, you are misusing the API...";

/* Return the next key-down record. Resize events repaint immediately and all
 * other console events are left out of the character stream. */
static int readConsoleKey(struct linenoiseState *l, KEY_EVENT_RECORD *key) {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    INPUT_RECORD ir;
    DWORD count;

    while (1) {
        if (!ReadConsoleInputW(hIn,&ir,1,&count) || count == 0) return -1;
        if (ir.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            if (l->in_completion && completionCallback != NULL)
                refreshLineWithCompletion(l,NULL,REFRESH_ALL);
            else
                refreshLine(l);
            continue;
        }
        if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
            *key = ir.Event.KeyEvent;
            return 1;
        }
    }
}

static int insertCodepoint(struct linenoiseState *l, uint32_t cp) {
    char utf8[4];
    int utf8len = utf8EncodeChar(cp,utf8);
    acceptDisplayedCompletion(l);
    return linenoiseEditInsert(l,utf8,(size_t)utf8len);
}

char *linenoiseEditFeed(struct linenoiseState *l) {
    if (!_isatty(l->ifd) && !getenv("LINENOISE_ASSUME_TTY")) return linenoiseNoTTY();

    char c = 0;
    KEY_EVENT_RECORD key;

    while (1) {
        WCHAR wc;
        WORD vkey;

        if (readConsoleKey(l,&key) == -1) {
            discardHistoryPlaceholder(l);
            return NULL;
        }
        wc = key.uChar.UnicodeChar;
        vkey = key.wVirtualKeyCode;

        if (pending_high_surrogate != 0 && !(wc >= 0xdc00 && wc <= 0xdfff)) {
            if (insertCodepoint(l,0xfffd) == -1) {
                discardHistoryPlaceholder(l);
                return NULL;
            }
            pending_high_surrogate = 0;
        }
        if (wc >= 0xd800 && wc <= 0xdbff) {
            pending_high_surrogate = wc;
            return linenoiseEditMore;
        }
        if (wc >= 0xdc00 && wc <= 0xdfff) {
            uint32_t cp = 0xfffd;
            if (pending_high_surrogate != 0) {
                cp = 0x10000 +
                     (((uint32_t)pending_high_surrogate - 0xd800) << 10) +
                     ((uint32_t)wc - 0xdc00);
            }
            pending_high_surrogate = 0;
            if (insertCodepoint(l,cp) == -1) {
                discardHistoryPlaceholder(l);
                return NULL;
            }
            return linenoiseEditMore;
        }
        if (wc > 0x7f) {
            if (insertCodepoint(l,wc) == -1) {
                discardHistoryPlaceholder(l);
                return NULL;
            }
            return linenoiseEditMore;
        }
        if (wc != 0) {
            c = (char)wc;
            break;
        }

        if (vkey == VK_UP) { linenoiseEditHistoryNext(l,1); return linenoiseEditMore; }
        if (vkey == VK_DOWN) { linenoiseEditHistoryNext(l,0); return linenoiseEditMore; }
        if (vkey == VK_RIGHT) { linenoiseEditMoveRight(l); return linenoiseEditMore; }
        if (vkey == VK_LEFT) { linenoiseEditMoveLeft(l); return linenoiseEditMore; }
        if (vkey == VK_HOME) { linenoiseEditMoveHome(l); return linenoiseEditMore; }
        if (vkey == VK_END) { linenoiseEditMoveEnd(l); return linenoiseEditMore; }
        if (vkey == VK_DELETE) { linenoiseEditDelete(l); return linenoiseEditMore; }
    }

    if ((l->in_completion || c == 9) && completionCallback != NULL) {
        int retval = completeLine(l,c);
        if (retval == 0) return linenoiseEditMore;
        c = retval;
    }
    switch(c) {
    case 13: /* Enter (\r) */
    case 10: /* Enter (\n) */
        discardHistoryPlaceholder(l);
        if (mlmode) linenoiseEditMoveEnd(l);
        if (hintsCallback) {
            linenoiseHintsCallback *hc = hintsCallback; hintsCallback = NULL;
            refreshLine(l); hintsCallback = hc;
        }
        return _strdup(l->buf);
    case 3: /* Ctrl-C */
        discardHistoryPlaceholder(l);
        errno = EAGAIN; return NULL;
    case 26: /* Ctrl-Z (EOF on Windows) */
        discardHistoryPlaceholder(l);
        errno = ENOENT; return NULL;
    case 127: case 8: linenoiseEditBackspace(l); break;
    case 4: /* Ctrl-D */
        if (l->len > 0) linenoiseEditDelete(l);
        else {
            discardHistoryPlaceholder(l);
            errno = ENOENT;
            return NULL;
        }
        break;
    case 20: /* Ctrl-T */
        if (l->pos > 0 && l->pos < l->len) {
            size_t prevlen = utf8PrevCharLen(l->buf,l->pos);
            size_t currlen = utf8NextCharLen(l->buf,l->pos,l->len);
            size_t prevstart = l->pos - prevlen;
            char sbo_buf[64];
            char *tmp = currlen <= sizeof(sbo_buf) ? sbo_buf : malloc(currlen);
            if (!tmp) break;

            memcpy(tmp,l->buf+l->pos,currlen);
            memmove(l->buf+prevstart+currlen,l->buf+prevstart,prevlen);
            memcpy(l->buf+prevstart,tmp,currlen);
            if (tmp != sbo_buf) free(tmp);
            if (l->pos + currlen <= l->len) l->pos += currlen;
            refreshLine(l);
        }
        break;
    case 2: linenoiseEditMoveLeft(l); break;
    case 6: linenoiseEditMoveRight(l); break;
    case 16: linenoiseEditHistoryNext(l, 1); break;
    case 14: linenoiseEditHistoryNext(l, 0); break;
    case 27: /* Standalone Escape. Navigation uses virtual-key records. */
        break;
    default:
        if (linenoiseEditInsert(l,&c,1) == -1) {
            discardHistoryPlaceholder(l);
            return NULL;
        }
        break;
    case 21: l->buf[0] = '\0'; l->pos = l->len = 0; refreshLine(l); break;
    case 11: l->buf[l->pos] = '\0'; l->len = l->pos; refreshLine(l); break;
    case 1: linenoiseEditMoveHome(l); break;
    case 5: linenoiseEditMoveEnd(l); break;
    case 12: linenoiseClearScreen(); refreshLine(l); break;
    case 23: linenoiseEditDeletePrevWord(l); break;
    }
    return linenoiseEditMore;
}

void linenoiseEditStop(struct linenoiseState *l) {
    discardHistoryPlaceholder(l);
    if (!_isatty(l->ifd) && !getenv("LINENOISE_ASSUME_TTY")) return;
    disableRawMode(l->ifd);
    printf("\n");
}

static char *linenoiseBlockingEdit(int stdin_fd, int stdout_fd, char *buf, size_t buflen, const char *prompt) {
    struct linenoiseState l;
    if (buflen == 0) { errno = EINVAL; return NULL; }
    if (linenoiseEditStart(&l,stdin_fd,stdout_fd,buf,buflen,prompt) == -1)
        return NULL;
    char *res;
    while((res = linenoiseEditFeed(&l)) == linenoiseEditMore);
    linenoiseEditStop(&l);
    return res;
}

char *linenoise(const char *prompt) {
    char buf[LINENOISE_MAX_LINE];
    if (!_isatty(STDIN_FILENO) && !getenv("LINENOISE_ASSUME_TTY")) return linenoiseNoTTY();
    else if (isUnsupportedTerm()) {
        printf("%s",prompt); fflush(stdout);
        if (fgets(buf,LINENOISE_MAX_LINE,stdin) == NULL) return NULL;
        size_t len = strlen(buf);
        while(len && (buf[len-1] == '\n' || buf[len-1] == '\r')) { len--; buf[len] = '\0'; }
        return _strdup(buf);
    } else return linenoiseBlockingEdit(STDIN_FILENO,STDOUT_FILENO,buf,LINENOISE_MAX_LINE,prompt);
}

void linenoiseFree(void *ptr) { if (ptr != linenoiseEditMore) free(ptr); }

void linenoiseClearScreen(void) { _write(STDOUT_FILENO,"\x1b[H\x1b[2J",7); }
void linenoiseSetMultiLine(int ml) { mlmode = ml; }
void linenoiseMaskModeEnable(void) { maskmode = 1; }
void linenoiseMaskModeDisable(void) { maskmode = 0; }

static void freeHistory(void) {
    if (history) { int j; for (j = 0; j < history_len; j++) free(history[j]); free(history); }
}

static void linenoiseAtExit(void) { disableRawMode(STDIN_FILENO); freeHistory(); }

int linenoiseHistoryAdd(const char *line) {
    char *linecopy;
    if (history_max_len == 0) return 0;
    if (history == NULL) {
        history = malloc(sizeof(char*)*history_max_len);
        if (history == NULL) return 0;
        memset(history,0,(sizeof(char*)*history_max_len));
    }
    if (history_len && !strcmp(history[history_len-1], line)) return 0;
    linecopy = _strdup(line);
    if (!linecopy) return 0;
    if (history_len == history_max_len) {
        free(history[0]);
        memmove(history,history+1,sizeof(char*)*(history_max_len-1));
        history_len--;
    }
    history[history_len] = linecopy;
    history_len++;
    return 1;
}

int linenoiseHistorySetMaxLen(int len) {
    if (len < 1) return 0;
    if (history) {
        char **new = malloc(sizeof(char *) * len);
        if (new == NULL) return 0;

        /* How many entries can we keep? */
        int tocopy = history_len < len ? history_len : len;

        /* Free entries that won't fit (the oldest ones) */
        for (int j = 0; j < history_len - tocopy; j++) {
            free(history[j]);
            history[j] = NULL;
        }

        memset(new, 0, sizeof(char *) * len);
        /* Copy the most-recent `tocopy` entries */
        memcpy(new, history + (history_len - tocopy), sizeof(char *) * tocopy);
        free(history);
        history = new;
        history_len = tocopy; /* keep history_len consistent */
    }
    history_max_len = len;
    /* Clamp in case history was NULL and history_len was already > len somehow */
    if (history_len > history_max_len) history_len = history_max_len;
    return 1;
}

int linenoiseHistorySave(const char *filename) {
    FILE *fp = fopen(filename,"w");
    if (fp == NULL) return -1;
    for (int j = 0; j < history_len; j++) fprintf(fp,"%s\n",history[j]);
    fclose(fp);
    return 0;
}

int linenoiseHistoryLoad(const char *filename) {
    FILE *fp = fopen(filename,"r");
    char buf[LINENOISE_MAX_LINE];
    if (fp == NULL) return -1;
    while (fgets(buf,LINENOISE_MAX_LINE,fp) != NULL) {
        char *p = strchr(buf,'\r');
        if (!p) p = strchr(buf,'\n');
        if (p) *p = '\0';
        linenoiseHistoryAdd(buf);
    }
    fclose(fp);
    return 0;
}

static char *linenoiseNoTTY(void) {
    char *line = NULL; size_t len = 0, maxlen = 0;
    while(1) {
        if (len == maxlen) {
            if (maxlen == 0) maxlen = 16;
            maxlen *= 2; char *oldval = line;
            line = realloc(line,maxlen);
            if (line == NULL) { if (oldval) free(oldval); return NULL; }
        }
        int c = fgetc(stdin);
        if (c == EOF || c == '\n') {
            if (c == EOF && len == 0) { free(line); return NULL; }
            else { line[len] = '\0'; return line; }
        } else { line[len] = (char)c; len++; }
    }
}

void linenoisePrintKeyCodes(void) {
    printf("Linenoise key codes debugging mode not fully implemented on Windows.\n");
}
