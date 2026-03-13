/*
 * Q+ Compiler (qpc) — Source File Manager (Implementation)
 */

#include "qpc/source.h"

/* ── Global source registry ──────────────────────────────────── */
static SourceFile g_sources[MAX_SOURCE_FILES];
static u16 g_source_count = 0;

void source_registry_init(void) {
    memset(g_sources, 0, sizeof(g_sources));
    g_source_count = 0;
}

/* ── Build line offset table ─────────────────────────────────── */
static void build_line_offsets(SourceFile *src) {
    /* Count lines first */
    u32 count = 1;  /* At least one line */
    for (usize i = 0; i < src->length; i++) {
        if (src->content[i] == '\n') count++;
    }

    src->line_offsets = (u32 *)malloc(count * sizeof(u32));
    src->line_count = count;

    /* Line 0 starts at offset 0 */
    src->line_offsets[0] = 0;
    u32 line = 1;
    for (usize i = 0; i < src->length; i++) {
        if (src->content[i] == '\n' && line < count) {
            src->line_offsets[line] = (u32)(i + 1);
            line++;
        }
    }
}

/* ── Load from file ──────────────────────────────────────────── */
i32 source_load(const char *path) {
    if (g_source_count >= MAX_SOURCE_FILES) {
        fprintf(stderr, "error: too many source files (max %d)\n", MAX_SOURCE_FILES);
        return -1;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: cannot open file '%s'\n", path);
        return -1;
    }

    /* Get file size */
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        fprintf(stderr, "error: cannot determine size of '%s'\n", path);
        return -1;
    }

    /* Read entire file */
    char *content = (char *)malloc((usize)size + 1);
    if (!content) {
        fclose(f);
        fprintf(stderr, "error: out of memory reading '%s'\n", path);
        return -1;
    }

    usize read = fread(content, 1, (usize)size, f);
    fclose(f);
    content[read] = '\0';

    /* Register */
    u16 id = g_source_count;
    SourceFile *src = &g_sources[id];
    src->id = id;
    src->path = path;
    src->content = content;
    src->length = read;
    g_source_count++;

    build_line_offsets(src);

    return (i32)id;
}

/* ── Load from string (for tests) ────────────────────────────── */
i32 source_load_string(const char *name, const char *content) {
    if (g_source_count >= MAX_SOURCE_FILES) {
        return -1;
    }

    usize len = strlen(content);
    char *copy = (char *)malloc(len + 1);
    memcpy(copy, content, len);
    copy[len] = '\0';

    u16 id = g_source_count;
    SourceFile *src = &g_sources[id];
    src->id = id;
    src->path = name;
    src->content = copy;
    src->length = len;
    g_source_count++;

    build_line_offsets(src);

    return (i32)id;
}

/* ── Get source by ID ────────────────────────────────────────── */
SourceFile *source_get(u16 id) {
    if (id >= g_source_count) return NULL;
    return &g_sources[id];
}

/* ── Get line/col from offset ────────────────────────────────── */
SourceLoc source_get_loc(const SourceFile *src, u32 offset) {
    SourceLoc loc = { 1, 1 };

    if (!src || !src->line_offsets) return loc;

    /* Binary search for the line containing this offset */
    u32 lo = 0;
    u32 hi = src->line_count - 1;

    while (lo <= hi) {
        u32 mid = (lo + hi) / 2;
        if (mid + 1 < src->line_count && src->line_offsets[mid + 1] <= offset) {
            lo = mid + 1;
        } else if (src->line_offsets[mid] > offset) {
            if (mid == 0) break;
            hi = mid - 1;
        } else {
            /* line_offsets[mid] <= offset < line_offsets[mid+1] */
            loc.line = mid + 1;  /* 1-indexed */
            loc.col = offset - src->line_offsets[mid] + 1;  /* 1-indexed */
            return loc;
        }
    }

    /* Fallback: first line */
    loc.line = 1;
    loc.col = offset + 1;
    return loc;
}

/* ── Get line text ───────────────────────────────────────────── */
StringView source_get_line_text(const SourceFile *src, u32 line) {
    StringView sv = { "", 0 };
    if (!src || line == 0 || line > src->line_count) return sv;

    u32 idx = line - 1;  /* 0-indexed */
    u32 start = src->line_offsets[idx];
    u32 end;

    if (idx + 1 < src->line_count) {
        end = src->line_offsets[idx + 1];
    } else {
        end = (u32)src->length;
    }

    /* Strip trailing \n or \r\n */
    while (end > start && (src->content[end - 1] == '\n' || src->content[end - 1] == '\r')) {
        end--;
    }

    sv.data = src->content + start;
    sv.length = end - start;
    return sv;
}
