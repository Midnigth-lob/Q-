/*
 * Q+ Compiler (qpc) — Common Definitions
 * Copyright (c) 2026 Q+ Project
 *
 * Standard includes, type aliases, and utility macros used
 * throughout the entire compiler codebase.
 */

#ifndef QPC_COMMON_H
#define QPC_COMMON_H

/* ── Standard headers ─────────────────────────────────────────── */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>
#include <ctype.h>

/* ── Fixed-width type aliases (matching Q+ naming) ────────────── */
typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;
typedef int8_t    i8;
typedef int16_t   i16;
typedef int32_t   i32;
typedef int64_t   i64;
typedef size_t    usize;
typedef float     f32;
typedef double    f64;

/* ── Compiler version ─────────────────────────────────────────── */
#define QPC_VERSION_MAJOR 0
#define QPC_VERSION_MINOR 1
#define QPC_VERSION_PATCH 0
#define QPC_VERSION_STRING "0.1.0"

/* ── Utility macros ───────────────────────────────────────────── */

/* Array element count */
#define ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

/* Min / Max */
#define QPC_MIN(a, b) ((a) < (b) ? (a) : (b))
#define QPC_MAX(a, b) ((a) > (b) ? (a) : (b))

/* Unreachable marker (for switch defaults that should never be hit) */
#define QPC_UNREACHABLE()                                         \
    do {                                                          \
        fprintf(stderr, "UNREACHABLE at %s:%d\n", __FILE__, __LINE__); \
        abort();                                                  \
    } while (0)

/* ── Arena Allocator (simple bump allocator for AST nodes) ────── */

#define ARENA_BLOCK_SIZE (1024 * 1024)  /* 1 MB blocks */

typedef struct ArenaBlock {
    u8 *data;
    usize used;
    usize capacity;
    struct ArenaBlock *next;
} ArenaBlock;

typedef struct Arena {
    ArenaBlock *current;
    ArenaBlock *first;
} Arena;

static inline void arena_init(Arena *arena) {
    ArenaBlock *block = (ArenaBlock *)malloc(sizeof(ArenaBlock));
    block->data = (u8 *)malloc(ARENA_BLOCK_SIZE);
    block->used = 0;
    block->capacity = ARENA_BLOCK_SIZE;
    block->next = NULL;
    arena->current = block;
    arena->first = block;
}

static inline void *arena_alloc(Arena *arena, usize size) {
    /* Align to 8 bytes */
    size = (size + 7) & ~(usize)7;

    if (arena->current->used + size > arena->current->capacity) {
        usize cap = ARENA_BLOCK_SIZE > size ? ARENA_BLOCK_SIZE : size;
        ArenaBlock *block = (ArenaBlock *)malloc(sizeof(ArenaBlock));
        block->data = (u8 *)malloc(cap);
        block->used = 0;
        block->capacity = cap;
        block->next = NULL;
        arena->current->next = block;
        arena->current = block;
    }

    void *ptr = arena->current->data + arena->current->used;
    arena->current->used += size;
    return ptr;
}

static inline void arena_free(Arena *arena) {
    ArenaBlock *block = arena->first;
    while (block) {
        ArenaBlock *next = block->next;
        free(block->data);
        free(block);
        block = next;
    }
    arena->current = NULL;
    arena->first = NULL;
}

/* ── String view (non-owning slice of a string) ──────────────── */

typedef struct StringView {
    const char *data;
    usize length;
} StringView;

static inline StringView sv_from_cstr(const char *s) {
    StringView sv;
    sv.data = s;
    sv.length = strlen(s);
    return sv;
}

static inline StringView sv_from_parts(const char *data, usize length) {
    StringView sv;
    sv.data = data;
    sv.length = length;
    return sv;
}

static inline bool sv_equals(StringView a, StringView b) {
    if (a.length != b.length) return false;
    return memcmp(a.data, b.data, a.length) == 0;
}

static inline bool sv_equals_cstr(StringView sv, const char *s) {
    usize len = strlen(s);
    if (sv.length != len) return false;
    return memcmp(sv.data, s, len) == 0;
}

#endif /* QPC_COMMON_H */
