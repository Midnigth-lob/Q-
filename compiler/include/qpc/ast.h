/*
 * Q+ Compiler (qpc) — Abstract Syntax Tree
 *
 * Defines all AST node types for the Q+ language.
 * Every node includes a Span for error reporting.
 * Nodes are arena-allocated (no individual free needed).
 */

#ifndef QPC_AST_H
#define QPC_AST_H

#include "common.h"
#include "token.h"

/* Forward declarations */
typedef struct AstNode  AstNode;
typedef struct AstType  AstType;

/* ── AST Node Kinds ──────────────────────────────────────────── */

typedef enum AstNodeKind {
    /* ── Top-level declarations ──────────────────────────── */
    AST_PROGRAM,            /* Root node: list of declarations */
    AST_MODULE_DECL,        /* module kernel::memory; */
    AST_IMPORT_DECL,        /* import path { items }; */
    AST_FN_DECL,            /* fn name(params) -> type { body } */
    AST_STRUCT_DECL,        /* struct Name { fields } */
    AST_ENUM_DECL,          /* enum Name { variants } */
    AST_UNION_DECL,         /* union Name { fields } */
    AST_TRAIT_DECL,         /* trait Name { methods } */
    AST_IMPL_BLOCK,         /* impl [Trait for] Type { methods } */
    AST_CONST_DECL,         /* const NAME: Type = expr; */
    AST_STATIC_DECL,        /* static NAME: Type = expr; */
    AST_TYPE_ALIAS,         /* type Name = Type; */
    AST_DRIVER_DECL,        /* driver Name { ... } */
    AST_SYSCALL_DECL,       /* syscall name(params) -> type { body } */
    AST_MMIO_DECL,          /* mmio Name at ADDR, size SIZE { ... } */

    /* ── Statements ──────────────────────────────────────── */
    AST_LET_STMT,           /* let [mut] name [: type] = expr; */
    AST_EXPR_STMT,          /* expr; */
    AST_RETURN_STMT,        /* return [expr]; */
    AST_BREAK_STMT,         /* break [label]; */
    AST_CONTINUE_STMT,      /* continue [label]; */
    AST_DEFER_STMT,         /* defer expr; */
    AST_ASSIGN_STMT,        /* lhs op= rhs; */

    /* ── Expressions ─────────────────────────────────────── */
    AST_INT_LIT,            /* 42, 0xFF */
    AST_FLOAT_LIT,          /* 3.14 */
    AST_STRING_LIT,         /* "hello" */
    AST_CHAR_LIT,           /* 'A' */
    AST_BOOL_LIT,           /* true, false */
    AST_NULL_LIT,           /* null */
    AST_IDENT,              /* my_var */
    AST_PATH,               /* kernel::memory::alloc */

    AST_BINARY_OP,          /* a + b, a && b, etc */
    AST_UNARY_OP,           /* -x, !x, &x, *x, etc */
    AST_CALL,               /* f(args) */
    AST_METHOD_CALL,        /* obj.method(args) */
    AST_FIELD_ACCESS,       /* obj.field */
    AST_INDEX,              /* arr[i] */
    AST_CAST,               /* expr as Type */
    AST_RANGE,              /* a..b, a..=b */

    AST_BLOCK,              /* { stmts; final_expr } */
    AST_IF,                 /* if cond { } else { } */
    AST_MATCH,              /* match expr { arms } */
    AST_MATCH_ARM,          /* pattern => body */
    AST_FOR,                /* for var in expr { } */
    AST_WHILE,              /* while cond { } */
    AST_LOOP,               /* loop { } */

    AST_REF,                /* &expr, &mut expr */
    AST_DEREF,              /* *expr */
    AST_STRUCT_LIT,         /* Type { field: val, ... } */
    AST_ARRAY_LIT,          /* [a, b, c] or [val; count] */
    AST_TUPLE_LIT,          /* (a, b) */

    AST_UNSAFE_BLOCK,       /* unsafe { ... } */
    AST_ASM_EXPR,           /* asm!("...", operands) */
    AST_ERROR_PROP,         /* expr? */

    AST_CLOSURE,            /* |args| body (if we add closures later) */

    AST_NODE_COUNT
} AstNodeKind;

/* ── Type AST ────────────────────────────────────────────────── */

typedef enum AstTypeKind {
    AST_TYPE_PRIMITIVE,     /* u8, i32, bool, void, etc */
    AST_TYPE_NAMED,         /* Point, PageTable */
    AST_TYPE_PATH,          /* kernel::memory::PageTable */
    AST_TYPE_ARRAY,         /* [T; N] */
    AST_TYPE_SLICE,         /* slice<T> */
    AST_TYPE_REF,           /* &T, &mut T */
    AST_TYPE_PTR,           /* ptr<T>, ptr<volatile T> */
    AST_TYPE_OWN,           /* own<T> */
    AST_TYPE_FN_PTR,        /* fn(A, B) -> C */
    AST_TYPE_TUPLE,         /* (A, B) */
    AST_TYPE_GENERIC,       /* T<A, B> */
    AST_TYPE_NEVER,         /* ! */
    AST_TYPE_INFERRED,      /* _ (let compiler infer) */
} AstTypeKind;

struct AstType {
    AstTypeKind kind;
    Span        span;

    union {
        /* AST_TYPE_PRIMITIVE */
        TokenKind primitive;  /* TOK_KW_U8, TOK_KW_I32, etc */

        /* AST_TYPE_NAMED / AST_TYPE_PATH */
        struct {
            StringView  name;
            AstNode    *path;       /* for qualified names */
        } named;

        /* AST_TYPE_ARRAY */
        struct {
            AstType *element;
            AstNode *size_expr;     /* compile-time const */
        } array;

        /* AST_TYPE_SLICE */
        struct {
            AstType *element;
        } slice;

        /* AST_TYPE_REF */
        struct {
            AstType *inner;
            bool     is_mut;
        } ref;

        /* AST_TYPE_PTR */
        struct {
            AstType *inner;
            bool     is_volatile;
        } ptr;

        /* AST_TYPE_OWN */
        struct {
            AstType *inner;
        } own;

        /* AST_TYPE_FN_PTR */
        struct {
            AstType **params;
            u32       param_count;
            AstType  *return_type;
        } fn_ptr;

        /* AST_TYPE_TUPLE */
        struct {
            AstType **elements;
            u32       count;
        } tuple;

        /* AST_TYPE_GENERIC */
        struct {
            AstType  *base;
            AstType **args;
            u32       arg_count;
        } generic;
    };
};

/* ── Attribute ───────────────────────────────────────────────── */

typedef struct AstAttribute {
    StringView name;        /* "no_mangle", "packed", etc */
    AstNode  **args;        /* optional arguments */
    u32        arg_count;
    Span       span;
} AstAttribute;

/* ── Function Parameter ──────────────────────────────────────── */

typedef struct AstParam {
    StringView name;
    AstType   *type;
    Span       span;
} AstParam;

/* ── Struct Field ────────────────────────────────────────────── */

typedef struct AstField {
    StringView name;
    AstType   *type;
    bool       is_pub;
    Span       span;
} AstField;

/* ── Enum Variant ────────────────────────────────────────────── */

typedef struct AstVariant {
    StringView  name;
    AstType   **payload_types;   /* for tagged unions: Ok(T) */
    u32         payload_count;
    AstNode    *discriminant;    /* for explicit values: Red = 0 */
    Span        span;
} AstVariant;

/* ── Binary Operator Kinds ───────────────────────────────────── */

typedef enum BinaryOp {
    BIN_ADD, BIN_SUB, BIN_MUL, BIN_DIV, BIN_MOD,
    BIN_BIT_AND, BIN_BIT_OR, BIN_BIT_XOR, BIN_SHL, BIN_SHR,
    BIN_EQ, BIN_NEQ, BIN_LT, BIN_GT, BIN_LT_EQ, BIN_GT_EQ,
    BIN_AND, BIN_OR,
    BIN_RANGE, BIN_RANGE_INCLUSIVE,
} BinaryOp;

/* ── Unary Operator Kinds ────────────────────────────────────── */

typedef enum UnaryOp {
    UNARY_NEG,          /* -x */
    UNARY_BIT_NOT,      /* ~x */
    UNARY_LOG_NOT,      /* not x */
    UNARY_REF,          /* &x */
    UNARY_REF_MUT,      /* &mut x */
    UNARY_DEREF,        /* *x */
} UnaryOp;

/* ── Assignment Operator Kinds ───────────────────────────────── */

typedef enum AssignOp {
    ASSIGN_EQ,          /* = */
    ASSIGN_ADD,         /* += */
    ASSIGN_SUB,         /* -= */
    ASSIGN_MUL,         /* *= */
    ASSIGN_DIV,         /* /= */
    ASSIGN_MOD,         /* %= */
    ASSIGN_BIT_AND,     /* &= */
    ASSIGN_BIT_OR,      /* |= */
    ASSIGN_BIT_XOR,     /* ^= */
    ASSIGN_SHL,         /* <<= */
    ASSIGN_SHR,         /* >>= */
} AssignOp;

/* ── Main AST Node ───────────────────────────────────────────── */

struct AstNode {
    AstNodeKind kind;
    Span        span;

    union {
        /* AST_PROGRAM */
        struct {
            AstNode **decls;
            u32       decl_count;
        } program;

        /* AST_MODULE_DECL */
        struct {
            AstNode *path;      /* path expression: kernel::memory */
        } module_decl;

        /* AST_IMPORT_DECL */
        struct {
            AstNode    *path;       /* module path */
            StringView *items;      /* { Item1, Item2 } */
            u32         item_count;
            StringView  alias;      /* as other_name */
            bool        import_all; /* ::* */
        } import_decl;

        /* AST_FN_DECL */
        struct {
            StringView     name;
            AstParam      *params;
            u32             param_count;
            AstType        *return_type;  /* NULL if void */
            AstNode        *body;         /* block or NULL for trait sigs */
            AstAttribute   *attribs;
            u32             attrib_count;
            bool            is_pub;
            bool            is_unsafe;
            bool            is_const;
            bool            is_interrupt;  /* interrupt fn */
        } fn_decl;

        /* AST_STRUCT_DECL */
        struct {
            StringView     name;
            AstField      *fields;
            u32             field_count;
            AstAttribute   *attribs;
            u32             attrib_count;
            bool            is_pub;
        } struct_decl;

        /* AST_ENUM_DECL */
        struct {
            StringView     name;
            AstVariant    *variants;
            u32             variant_count;
            AstType        *repr_type;    /* : u8 */
            bool            is_pub;
        } enum_decl;

        /* AST_TRAIT_DECL */
        struct {
            StringView  name;
            AstNode   **methods;
            u32         method_count;
            bool        is_pub;
        } trait_decl;

        /* AST_IMPL_BLOCK */
        struct {
            AstType    *target_type;
            StringView  trait_name;   /* empty if plain impl */
            AstNode   **methods;
            u32         method_count;
        } impl_block;

        /* AST_CONST_DECL / AST_STATIC_DECL */
        struct {
            StringView  name;
            AstType    *type;
            AstNode    *value;
            bool        is_pub;
            bool        is_mut;     /* for static only */
        } const_decl;

        /* AST_TYPE_ALIAS */
        struct {
            StringView  name;
            AstType    *target;
            bool        is_pub;
        } type_alias;

        /* AST_DRIVER_DECL */
        struct {
            StringView     name;
            AstField      *fields;
            u32             field_count;
            AstNode       **methods;
            u32             method_count;
            bool            is_pub;
        } driver_decl;

        /* AST_SYSCALL_DECL */
        struct {
            StringView  name;
            AstParam   *params;
            u32         param_count;
            AstType    *return_type;
            AstNode    *body;
        } syscall_decl;

        /* AST_LET_STMT */
        struct {
            StringView  name;
            AstType    *type;       /* optional */
            AstNode    *init;       /* required */
            bool        is_mut;
        } let_stmt;

        /* AST_EXPR_STMT */
        struct {
            AstNode *expr;
        } expr_stmt;

        /* AST_RETURN_STMT */
        struct {
            AstNode *value;     /* optional */
        } return_stmt;

        /* AST_BREAK_STMT / AST_CONTINUE_STMT */
        struct {
            StringView label;   /* optional */
        } loop_ctrl;

        /* AST_DEFER_STMT */
        struct {
            AstNode *expr;
        } defer_stmt;

        /* AST_ASSIGN_STMT */
        struct {
            AstNode  *target;
            AstNode  *value;
            AssignOp  op;
        } assign_stmt;

        /* AST_INT_LIT */
        struct {
            u64     value;
            IntBase base;
        } int_lit;

        /* AST_FLOAT_LIT */
        struct {
            f64 value;
        } float_lit;

        /* AST_STRING_LIT */
        struct {
            StringView value;
            bool       is_raw;
        } string_lit;

        /* AST_CHAR_LIT */
        struct {
            u32 value;
        } char_lit;

        /* AST_BOOL_LIT */
        struct {
            bool value;
        } bool_lit;

        /* AST_IDENT */
        struct {
            StringView name;
        } ident;

        /* AST_PATH: a::b::c */
        struct {
            StringView *segments;
            u32         segment_count;
        } path;

        /* AST_BINARY_OP */
        struct {
            BinaryOp  op;
            AstNode  *left;
            AstNode  *right;
        } binary;

        /* AST_UNARY_OP */
        struct {
            UnaryOp  op;
            AstNode *operand;
        } unary;

        /* AST_CALL */
        struct {
            AstNode  *callee;
            AstNode **args;
            u32       arg_count;
        } call;

        /* AST_METHOD_CALL */
        struct {
            AstNode    *object;
            StringView  method_name;
            AstNode   **args;
            u32         arg_count;
        } method_call;

        /* AST_FIELD_ACCESS */
        struct {
            AstNode    *object;
            StringView  field_name;
        } field_access;

        /* AST_INDEX */
        struct {
            AstNode *object;
            AstNode *index;
        } index;

        /* AST_CAST */
        struct {
            AstNode *expr;
            AstType *target_type;
        } cast;

        /* AST_BLOCK */
        struct {
            AstNode **stmts;
            u32       stmt_count;
            AstNode  *final_expr;   /* final expression (no semicolon) */
        } block;

        /* AST_IF */
        struct {
            AstNode *condition;
            AstNode *then_block;
            AstNode *else_block;    /* block or if-expr */
        } if_expr;

        /* AST_MATCH */
        struct {
            AstNode  *expr;
            AstNode **arms;
            u32       arm_count;
        } match_expr;

        /* AST_MATCH_ARM */
        struct {
            AstNode *pattern;
            AstNode *guard;     /* optional: if cond */
            AstNode *body;
        } match_arm;

        /* AST_FOR */
        struct {
            StringView  var_name;
            AstNode    *iterable;
            AstNode    *body;
            StringView  label;
        } for_loop;

        /* AST_WHILE */
        struct {
            AstNode    *condition;
            AstNode    *body;
            StringView  label;
        } while_loop;

        /* AST_LOOP */
        struct {
            AstNode    *body;
            StringView  label;
        } loop_expr;

        /* AST_STRUCT_LIT */
        struct {
            AstType    *type_name;
            StringView *field_names;
            AstNode   **field_values;
            u32         field_count;
        } struct_lit;

        /* AST_ARRAY_LIT */
        struct {
            AstNode **elements;
            u32       element_count;
            AstNode  *repeat_value;  /* [val; count] syntax */
            AstNode  *repeat_count;
        } array_lit;

        /* AST_UNSAFE_BLOCK */
        struct {
            AstNode *body;
        } unsafe_block;

        /* AST_ASM_EXPR */
        struct {
            StringView  template_str;
            /* operands TBD */
        } asm_expr;

        /* AST_ERROR_PROP (?) */
        struct {
            AstNode *expr;
        } error_prop;

        /* AST_REF / AST_DEREF */
        struct {
            AstNode *expr;
            bool     is_mut;
        } ref_expr;

        /* AST_RANGE */
        struct {
            AstNode *start;
            AstNode *end;
            bool     inclusive;
        } range;
    };
};

/* ── AST Node Constructor Helpers ────────────────────────────── */

/* Allocate a new AST node from the arena */
static inline AstNode *ast_new(Arena *arena, AstNodeKind kind, Span span) {
    AstNode *node = (AstNode *)arena_alloc(arena, sizeof(AstNode));
    memset(node, 0, sizeof(AstNode));
    node->kind = kind;
    node->span = span;
    return node;
}

/* Allocate a new AST type from the arena */
static inline AstType *ast_type_new(Arena *arena, AstTypeKind kind, Span span) {
    AstType *type = (AstType *)arena_alloc(arena, sizeof(AstType));
    memset(type, 0, sizeof(AstType));
    type->kind = kind;
    type->span = span;
    return type;
}

/* Allocate an array of pointers in the arena */
static inline AstNode **ast_node_array(Arena *arena, u32 count) {
    return (AstNode **)arena_alloc(arena, count * sizeof(AstNode *));
}

/* ── AST Debug Print ─────────────────────────────────────────── */

/* Print an AST tree with indentation (for debugging) */
void ast_print(const AstNode *node, int indent);
void ast_type_print(const AstType *type);

/* Get human-readable name for node kind */
const char *ast_node_kind_str(AstNodeKind kind);

#endif /* QPC_AST_H */
