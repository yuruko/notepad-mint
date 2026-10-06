/* tabdefs.h - record types shared by the generated tables (real_tab.c, w32_tab.c) and cmp.c.
 * no includes on purpose: the table units must not pull in a crt header (w32.h typedefs size_t itself).
 * every table ends with an entry whose first member is 0. */
#ifndef LC_TABDEFS_H
#define LC_TABDEFS_H

typedef struct { const char *name; unsigned offset, size; } TabField;
typedef struct { const char *name; unsigned size; const TabField *fields; } TabStruct;
typedef struct { const char *name; unsigned long long value; } TabConst;
typedef struct { const char *call; long long value; } TabMacro;
typedef struct { const char *name; unsigned size; int sign; } TabType;   /* sign: 1 signed, 0 unsigned, -1 pointer (size only) */

/* one set of tables per header; the prefix lets real_tab.obj and w32_tab.obj link into one program */
#define LC_TABLES(p) \
    extern const TabStruct p##structs[]; \
    extern const TabConst  p##consts[];  \
    extern const TabMacro  p##macros[];  \
    extern const TabType   p##types[];
LC_TABLES(real_)
LC_TABLES(w32_)

#endif
