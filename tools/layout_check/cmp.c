/* cmp.c - diffs the sdk tables (real_tab.c) against the w32.h tables (w32_tab.c) and reads the api link log.
 *   cmp <api_link.log> <link exit code> <api_names.txt> [<implib.txt>]
 * prints every difference, then one summary line per category.
 * exit code: 0 clean, 1 mismatches, 2 tool error (tables of different shape, unreadable file, the link failed for
 * some other reason than unresolved symbols).
 * host program: normal crt, built by check.bat with the same x86 cl. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tabdefs.h"

typedef struct { unsigned checked, bad; } Stat;

static int tool_error;

static void toolerr(const char *what, const char *name)
{
    printf("TOOL ERROR: %s: %s\n", what, name);
    tool_error = 1;
}

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *b;

    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (char *)malloc((size_t)n + 1);
    if (!b) { fclose(f); return 0; }
    n = (long)fread(b, 1, (size_t)n, f);
    b[n] = 0;
    fclose(f);
    return b;
}

/* ---------------------------------------------------------------- structs -- */

static const TabStruct *find_struct(const TabStruct *tab, const char *name)
{
    const TabStruct *s;
    for (s = tab; s->name; s++)
        if (!strcmp(s->name, name)) return s;
    return 0;
}

static const TabField *find_field(const TabStruct *s, const char *name)
{
    const TabField *f;
    for (f = s->fields; f->name; f++)
        if (!strcmp(f->name, name)) return f;
    return 0;
}

static void check_structs(Stat *ss, Stat *sf)
{
    const TabStruct *w, *r;
    const TabField *wf, *rf;
    int bad;

    for (w = w32_structs; w->name; w++) {
        r = find_struct(real_structs, w->name);
        if (!r) { toolerr("struct missing from the sdk table", w->name); continue; }
        ss->checked++;
        if (w->size != r->size) {
            printf("STRUCT %s: sizeof %u (w32.h) vs %u (sdk)\n", w->name, w->size, r->size);
            ss->bad++;
        }
        for (wf = w->fields; wf->name; wf++) {
            rf = find_field(r, wf->name);
            if (!rf) { toolerr("field missing from the sdk table", wf->name); continue; }
            sf->checked++;
            bad = 0;
            if (wf->offset != rf->offset) {
                printf("STRUCT %s.%s: offset %u (w32.h) vs %u (sdk)\n", w->name, wf->name, wf->offset, rf->offset);
                bad = 1;
            }
            if (wf->size != rf->size) {
                printf("STRUCT %s.%s: sizeof %u (w32.h) vs %u (sdk)\n", w->name, wf->name, wf->size, rf->size);
                bad = 1;
            }
            sf->bad += bad;
        }
    }
    for (r = real_structs; r->name; r++)
        if (!find_struct(w32_structs, r->name)) toolerr("struct missing from the w32.h table", r->name);
    if (!ss->checked) toolerr("nothing could be compared", "structs");
}

/* -------------------------------------------------------------- constants -- */

static void check_consts(Stat *s, unsigned *unverified)
{
    const TabConst *w, *r;
    unsigned nw = 0;

    for (w = w32_consts; w->name; w++) {
        nw++;
        for (r = real_consts; r->name && strcmp(r->name, w->name); r++) ;
        if (!r->name) {
            printf("UNVERIFIED CONST %s\n", w->name);
            (*unverified)++;
            continue;
        }
        s->checked++;
        if (w->value != r->value) {
            printf("CONST %s: 0x%llx (w32.h) vs 0x%llx (sdk)\n", w->name, w->value, r->value);
            s->bad++;
        }
    }
    for (r = real_consts; r->name; r++) {
        for (w = w32_consts; w->name && strcmp(r->name, w->name); w++) ;
        if (!w->name) toolerr("constant missing from the w32.h table", r->name);
    }
    if (nw && !s->checked) toolerr("nothing could be compared", "constants");
}

/* ----------------------------------------------------------------- macros -- */

static void check_macros(Stat *s, unsigned *unverified)
{
    const TabMacro *w, *r;
    unsigned nw = 0;

    for (w = w32_macros; w->call; w++) {
        nw++;
        for (r = real_macros; r->call && strcmp(r->call, w->call); r++) ;
        if (!r->call) {
            printf("UNVERIFIED MACRO %s\n", w->call);
            (*unverified)++;
            continue;
        }
        s->checked++;
        if (w->value != r->value) {
            printf("MACRO %s: %lld (w32.h) vs %lld (sdk)\n", w->call, w->value, r->value);
            s->bad++;
        }
    }
    for (r = real_macros; r->call; r++) {
        for (w = w32_macros; w->call && strcmp(r->call, w->call); w++) ;
        if (!w->call) toolerr("macro missing from the w32.h table", r->call);
    }
    if (nw && !s->checked) toolerr("nothing could be compared", "macros");
}

/* ------------------------------------------------------------------ types -- */

static const char *signname(int sign)
{
    return sign > 0 ? "signed" : sign == 0 ? "unsigned" : "pointer";
}

static void check_types(Stat *s)
{
    const TabType *w, *r;
    int bad;

    for (w = w32_types; w->name; w++) {
        for (r = real_types; r->name && strcmp(r->name, w->name); r++) ;
        if (!r->name) { toolerr("type missing from the sdk table", w->name); continue; }
        s->checked++;
        bad = 0;
        if (w->size != r->size) {
            printf("TYPE %s: sizeof %u (w32.h) vs %u (sdk)\n", w->name, w->size, r->size);
            bad = 1;
        }
        if (w->sign != r->sign) {
            printf("TYPE %s: %s (w32.h) vs %s (sdk)\n", w->name, signname(w->sign), signname(r->sign));
            bad = 1;
        }
        s->bad += bad;
    }
    for (r = real_types; r->name; r++) {
        for (w = w32_types; w->name && strcmp(r->name, w->name); w++) ;
        if (!w->name) toolerr("type missing from the w32.h table", r->name);
    }
    if (!s->checked) toolerr("nothing could be compared", "types");
}

/* -------------------------------------------------------------------- api -- */

#define MAXAPI 2048

/* needle inside [p, e), or 0 */
static const char *find_in(const char *p, const char *e, const char *needle)
{
    size_t n = strlen(needle);
    for (; p + n <= e; p++)
        if (!strncmp(p, needle, n)) return p;
    return 0;
}

/* what the import libraries export under this name (from dumpbin /linkermember:1 output):
 * >= 0 the stdcall argument bytes, -3 cdecl, -1 nothing, -2 no information */
static int implib_bytes(const char *lib, const char *name)
{
    size_t n = strlen(name), len;
    const char *p, *e, *end, *t;

    if (!lib) return -2;
    for (p = lib; *p; p = *e ? e + 1 : e) {
        e = strchr(p, '\n');
        if (!e) e = p + strlen(p);
        end = e;
        while (end > p && (end[-1] == '\r' || end[-1] == ' ')) end--;
        t = end;
        while (t > p && t[-1] != ' ') t--;
        len = (size_t)(end - t);
        if (len < n + 1 || t[0] != '_' || strncmp(t + 1, name, n)) continue;
        if (len == n + 1) return -3;
        if (t[n + 1] == '@') return atoi(t + n + 2);
    }
    return -1;
}

static void check_api(const char *logpath, int linkrc, const char *namespath, const char *libpath, Stat *s)
{
    char *names = slurp(namespath), *log = slurp(logpath), *lib = libpath ? slurp(libpath) : 0;
    const char *api[MAXAPI];
    char seen[MAXAPI];
    int napi = 0, i, others = 0, found = 0;
    char *p, *next;
    const char *lp, *e;

    if (!names) { toolerr("cannot read", namespath); goto done; }
    if (!log) { toolerr("cannot read", logpath); goto done; }
    if (lib && !strstr(lib, "public symbols")) {       /* not a dumpbin listing (dumpbin missing?): no hint then */
        free(lib);
        lib = 0;
    }

    for (p = names; *p; p = next) {
        char *nl = strchr(p, '\n');
        if (nl) {
            *nl = 0;
            next = nl + 1;
            if (nl > p && nl[-1] == '\r') nl[-1] = 0;
        } else {
            next = p + strlen(p);
        }
        if (!*p) continue;
        if (napi == MAXAPI) { toolerr("too many api names, raise MAXAPI in cmp.c", namespath); goto done; }
        api[napi] = p;
        seen[napi++] = 0;
    }
    s->checked = (unsigned)napi;

    for (lp = log; *lp; lp = *e ? e + 1 : e) {
        char raw[256], sym[256], *at;
        const char *u, *d;
        int bytes, hint, len;

        e = strchr(lp, '\n');
        if (!e) e = lp + strlen(lp);
        u = find_in(lp, e, "unresolved external symbol ");
        if (!u) {
            if (find_in(lp, e, "error LNK") && !find_in(lp, e, "LNK1120")) {
                len = (int)(e - lp);
                if (len && lp[len - 1] == '\r') len--;
                printf("LINK: %.*s\n", len, lp);
                others++;
            }
            continue;
        }
        u += strlen("unresolved external symbol ");
        for (i = 0; u + i < e && u[i] != ' ' && u[i] != '\r' && i < 255; i++) raw[i] = u[i];
        raw[i] = 0;
        found++;

        /* __imp__Name@N, or the import thunk _Name@N that a data table refers to; cdecl has no @N */
        d = raw;
        if (!strncmp(d, "__imp_", 6)) d += 6;
        if (*d == '_') d++;
        memcpy(sym, d, strlen(d) + 1);                 /* d points into raw[256], so it fits */
        at = strrchr(sym, '@');
        bytes = -1;
        if (at) { bytes = atoi(at + 1); *at = 0; }

        for (i = 0; i < napi && strcmp(api[i], sym); i++) ;
        if (i == napi) { toolerr("unexpected unresolved symbol", raw); continue; }
        if (seen[i]) continue;
        seen[i] = 1;
        s->bad++;
        hint = implib_bytes(lib, sym);
        if (bytes < 0) printf("API MISMATCH: %s (w32.h declares it cdecl", sym);
        else printf("API MISMATCH: %s (w32.h declares %d bytes of arguments", sym, bytes);
        if (hint >= 0) printf("; the import library exports %s@%d", sym, hint);
        else if (hint == -3) printf("; the import library exports it as cdecl");
        else if (hint == -1) printf("; the import library has no export of that name");
        printf(")\n");
    }
    if (others) toolerr("the api link printed errors other than unresolved symbols, see", logpath);
    else if (linkrc != 0 && !found) toolerr("the api link failed, see", logpath);

done:
    free(names);
    free(log);
    free(lib);
}

/* ------------------------------------------------------------------- main -- */

int main(int argc, char **argv)
{
    Stat ss = {0, 0}, sf = {0, 0}, sc = {0, 0}, sm = {0, 0}, st = {0, 0}, sa = {0, 0};
    unsigned unverified = 0, bad;

    if (argc < 4) {
        fprintf(stderr, "usage: cmp <api_link.log> <link exit code> <api_names.txt> [<implib.txt>]\n");
        return 2;
    }
    check_structs(&ss, &sf);
    check_consts(&sc, &unverified);
    check_macros(&sm, &unverified);
    check_types(&st);
    check_api(argv[1], atoi(argv[2]), argv[3], argc > 4 ? argv[4] : 0, &sa);

    printf("structs: %u checked, %u mismatches\n", ss.checked, ss.bad);
    printf("fields: %u checked, %u mismatches\n", sf.checked, sf.bad);
    printf("constants: %u checked, %u mismatches\n", sc.checked, sc.bad);
    printf("macros: %u checked, %u mismatches\n", sm.checked, sm.bad);
    printf("types: %u checked, %u mismatches\n", st.checked, st.bad);
    printf("api: %u checked, %u mismatches\n", sa.checked, sa.bad);
    printf("unverified: %u (informational, not a failure)\n", unverified);

    bad = ss.bad + sf.bad + sc.bad + sm.bad + st.bad + sa.bad;
    if (tool_error) return 2;
    return bad ? 1 : 0;
}
