/*
 * Q+ Compiler (qpc) — Source File Manager
 *
 * Loads source files into memory and provides source location
 * tracking (line/column from byte offset).
 */

#ifndef QPC_SOURCE_H
#define QPC_SOURCE_H

#include "common.h"

#define MAX_SOURCE_FILES 256

typedef struct SourceFile {
    u16         id;
    const char *path;
    char       *content;
    usize       length;

    /* Line offset table: line_offsets[i] = byte offset of line i (0-indexed) */
    u32  *line_offsets;
    u32   line_count;
} SourceFile;

typedef struct SourceLoc {
    u32 line;   /* 1-indexed */
    u32 col;    /* 1-indexed */
} SourceLoc;

/* Initialize the source file registry */
void source_registry_init(void);

/* Load a source file from disk. Returns the source ID, or -1 on error. */
i32 source_load(const char *path);

/* Load a source from a string (for tests). Returns the source ID. */
i32 source_load_string(const char *name, const char *content);

/* Get source file by ID */
SourceFile *source_get(u16 id);

/* Convert a byte offset to line/col */
SourceLoc source_get_loc(const SourceFile *src, u32 offset);

/* Get the text of a specific line (1-indexed). Returns StringView. */
StringView source_get_line_text(const SourceFile *src, u32 line);

#endif /* QPC_SOURCE_H */
