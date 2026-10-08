/* Included by unit.c: regression coverage for search boundaries, KMP and SSE2 tails. */
API LPVOID WINAPI VirtualAlloc(LPVOID, size_t, DWORD, DWORD);
API BOOL WINAPI VirtualProtect(LPVOID, size_t, DWORD, DWORD *);
API BOOL WINAPI VirtualFree(LPVOID, size_t, DWORD);

static int RefWord(WCHAR c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static int RefSearch(const WCHAR *t, int n, const WCHAR *p, int m, int from, int up, int mc, int word)
{
    int i, k, step = up ? -1 : 1;
    if (from < 0) from = 0;
    if (from > n) from = n;
    for (i = up ? from - m : from; i >= 0 && i <= n - m; i += step) {
        for (k = 0; k < m; k++) if (mc ? t[i + k] != p[k] : wlow(t[i + k]) != wlow(p[k])) break;
        if (k == m && (!word || ((!i || !RefWord(t[i - 1])) && (i + m == n || !RefWord(t[i + m]))))) return i;
    }
    return -1;
}

static void TestSearchExtra(void)
{
    static const struct { const WCHAR *text, *pat; int from, up, mc, at; } words[] = {
        { L"scatter cat cat_1 cat2 cat.", L"cat", 0, 0, 1, 8 },
        { L"scatter cat cat_1 cat2 cat.", L"cat", 27, 1, 1, 23 },
        { L"scatter cat cat_1 cat2 cat.", L"cat", 23, 1, 1, 8 },
        { L"Cat CAT cat", L"cat", 0, 0, 0, 0 },
        { L"Cat CAT cat", L"cat", 0, 0, 1, 8 },
        { L"cat\u0301 cat", L"cat", 0, 0, 1, 5 },
        { L"cat\u093f cat", L"cat", 0, 0, 1, 5 },
        { L"\u65e5cat cat", L"cat", 0, 0, 1, 5 },
        { L"cat\u0661 cat", L"cat", 0, 0, 1, 5 },
        { L"\u00e9lan \u00e9lanx", L"\u00c9LAN", 0, 0, 0, 0 },
        { L"cat-cat", L"cat", 1, 0, 1, 4 },
        { L"\xD801\xDC00" L"cat cat", L"cat", 0, 0, 1, 6 },
        { L"cat\xD801\xDC00" L" cat", L"cat", 0, 0, 1, 6 },
        { L"\xD83D\xDE00", L"\xD83D\xDE00", 0, 0, 1, 0 },
        { L"\xD83D\xDE00", L"\xD83D", 0, 0, 1, -1 },
        { L"\xD83D\xDE00", L"\xDE00", 0, 0, 1, -1 },
    };
    static const WCHAR alphabet[] = L"abAB 1_.-";
    WCHAR text[80], pat[520], name[96], *out, *guard, *edge;
    unsigned seed = 918273u;
    int i, j, n, m, from, up, mc, word, got, want, len, count, bad = 0;
    DWORD old;

    Group(L"search.c whole word / linear search");
    for (i = 0; i < COUNTOF(words); i++) {
        wsprintfW(name, L"whole-word boundary case %d", i);
        Int(name, FindInTextEx(words[i].text, wlen(words[i].text), words[i].pat, wlen(words[i].pat), words[i].from,
                              words[i].up, words[i].mc, 1), words[i].at);
    }
    Int(L"plain find cannot split a high surrogate from its pair", FindInText(L"\xD83D\xDE00", 2, L"\xD83D", 1, 0, 0, 1), -1);
    Int(L"plain reverse find cannot split a low surrogate from its pair", FindInText(L"\xD83D\xDE00", 2, L"\xDE00", 1, 2, 1, 1), -1);
    out = ReplaceAllTextEx(WLIT(L"cat scatter cat_1 CAT cat2 cat\u0301"), L"cat", 3, L"fox", 3, 0, 1, &len, &count);
    WantStr(L"whole-word output", out, len, WLIT(L"fox scatter cat_1 fox cat2 cat\u0301"));
    Want(count == 2, L"expected two replacements, got %d", count, 0);
    Done(L"whole-word replacement preserves embedded / combining-mark words");
    mem_free(out);
    out = ReplaceAllTextEx(L"cat cat", 7, L"cat", 3, L"x", 0x7FFFFFFF, 1, 1, &len, &count);
    Want(!out && !len && !count, L"overflow was not rejected", 0, 0);
    Done(L"replace expansion overflow fails before reading replacement bytes");
    mem_free(out);

    for (i = 0; i < 20000; i++) {
        seed = seed * 1664525u + 1013904223u;
        n = (int)((seed >> 16) % 70u) + 1;
        m = (int)((seed >> 24) % 12u) + 1;
        from = (int)((seed >> 8) % (unsigned)(n + 7)) - 3;
        up = (seed >> 2) & 1; mc = (seed >> 3) & 1; word = (seed >> 4) & 1;
        for (j = 0; j < n; j++) { seed = seed * 1664525u + 1013904223u; text[j] = alphabet[(seed >> 16) % 9u]; }
        for (j = 0; j < m; j++) { seed = seed * 1664525u + 1013904223u; pat[j] = alphabet[(seed >> 16) % 9u]; }
        if ((i & 3) == 0 && m <= n) memcpy(pat, text + (n - m) / 2, (size_t)m * sizeof(WCHAR));
        want = RefSearch(text, n, pat, m, from, up, mc, word);
        got = FindInTextEx(text, n, pat, m, from, up, mc, word);
        if (want != got) { Want(FALSE, L"case %d: got %d (reference differs)", i, got); break; }
    }
    Done(L"20000 randomized bounded searches match independent reference in both directions / options");

    out = (WCHAR *)mem_alloc(20000 * sizeof(WCHAR));
    if (out) {
        for (i = 0; i < 20000; i++) out[i] = 'a';
        for (i = 0; i < 511; i++) pat[i] = 'a';
        pat[511] = 'b';
        Int(L"long repeated-prefix needle: miss", FindInText(out, 20000, pat, 512, 0, 0, 1), -1);
        out[19999] = 'b';
        Int(L"long repeated-prefix needle: final match", FindInText(out, 20000, pat, 512, 0, 0, 1), 19488);
        Int(L"long reversed pattern on heap", FindInText(out, 20000, pat, 512, 20000, 1, 1), 19488);
        mem_free(out);
    } else Result(L"long search allocation", L"out of memory");

    Group(L"rt.asm dense masks / protected tails");
    for (i = 0; i < 256; i++) {
        for (j = 0; j < 64; j++) text[j] = (WCHAR)((i & (1 << (j & 7))) ? '\n' : 0x0A0A);
        for (from = 0; from < 8; from++) for (n = 0; n <= 40; n++)
            if ((int)mp_count_lf(text + from, (size_t)n) != Naive(text + from, n)) bad++;
    }
    Int(L"every 8-word newline mask x 8 alignments x 41 tails", bad, 0);
    guard = (WCHAR *)VirtualAlloc(NULL, 8192, 0x3000, 4); /* reserve+commit, read/write */
    if (guard && VirtualProtect((BYTE *)guard + 4096, 4096, 1, &old)) {
        for (n = 0; n <= 80; n++) {
            edge = (WCHAR *)((BYTE *)guard + 4096) - n;
            for (i = 0; i < n; i++) edge[i] = '\n';
            Want((int)mp_count_lf(edge, (size_t)n) == n, L"dense tail length %d", n, 0);
            if (n) Want(FindInText(edge, n, L"\n\nx", 3, 0, 0, 1) == -1, L"search read guard at length %d", n, 0);
        }
        Done(L"newline count / search never read past protected page (lengths 0..80)");
    } else Result(L"protected-tail allocation", L"VirtualAlloc / VirtualProtect failed");
    if (guard) VirtualFree(guard, 0, 0x8000);
}
