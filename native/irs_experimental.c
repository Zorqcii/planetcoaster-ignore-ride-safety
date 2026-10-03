/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Zorqcii
 *
 * EXPERIMENTAL: unfinished / untested rides  (included at the end of irs_patch.c)
 *
 * Diagnostic stage 1 (0.2.0-exp.1). Nothing here is active unless the player ticks the
 * experimental option; the stable fear/nausea options never depend on this file.
 *
 *  1) Open gate: the attraction "may open" test at 0x1405380f0 rejects attractions whose
 *     "tested / has ratings" flag is 0 (`je` at 0x14053812e). That `je` becomes a 2-byte nop,
 *     so untested and unfinished rides can be opened. Every other open requirement remains.
 *
 *  2) Ride list from Lua: the Lua side knows which rides are untested (public script API).
 *     It sends their station ids through a one-way channel made only of distinct functions
 *     (begin / bit0 / bit1 / push / commit); no Lua state is read.
 *
 *  3) Observe-only guest hook: in the guest ride evaluator, `mov eax,[r12+0x64]` at 0x1406a051b
 *     (the read of the "destination has ratings" flags) is replaced by a call to exp_observe,
 *     which performs the same read and, when the destination is unrated, records its id. It never
 *     changes any register except eax (as the original instruction did) and never changes the
 *     game's decision. The recorded ids are written to the log, to confirm that the guest code's
 *     destination id is the same station id the Lua side reports, before any later stage relies on it.
 */

__declspec(dllimport) void *__stdcall VirtualAlloc(void *, uintptr_t, DWORD, DWORD);

/* ---- patch sites ------------------------------------------------------------------------- */
static struct site g_exp_sites[] = {
    {0x14053812eULL, 2, {0x74, 0x0c}, {0x66, 0x90}},                    /* open gate: je -> nop */
    {0x1406a051bULL, 5, {0x41, 0x8b, 0x44, 0x24}, {0}},                  /* evaluator flags read (5th byte below) */
};
static const uint8_t g_exp_site1_orig[5] = {0x41, 0x8b, 0x44, 0x24, 0x64}; /* mov eax,[r12+0x64] */
static uint8_t g_exp_site1_patch[5];                                      /* call <stub>, filled at runtime */

static const struct fp g_exp_fps[] = {
    /* open gate: mov ecx,[rdi+0xc] ; test al,al ; jne ; test ecx,ecx ; (je) ; mov eax,[rdi+0x10] */
    {0x140538125ULL, 9, {0x8b, 0x4f, 0x0c, 0x84, 0xc0, 0x75, 0x10, 0x85, 0xc9}},
    {0x140538130ULL, 3, {0x8b, 0x47, 0x10}},
    /* evaluator candidate loop: mov r12,[r14] ; mov [rbp-0x40],r12 ; cmp qword [r12+0x10],0 ; mov rsi,[r12] */
    {0x1406a0500ULL, 17, {0x4d, 0x8b, 0x26, 0x4c, 0x89, 0x65, 0xc0, 0x49, 0x83, 0x7c, 0x24, 0x10, 0x00,
                          0x49, 0x8b, 0x34, 0x24}},
    /* (mov eax,[r12+0x64]) ; and eax,0xd ; cmp al,0xd */
    {0x1406a0520ULL, 5, {0x83, 0xe0, 0x0d, 0x3c, 0x0d}},
};

static int g_exp_supported = -1;
static int g_exp_state;            /* 0 off, 1 on */
static uint8_t *g_exp_stub;        /* near stub page */

static int exp_supported(void)
{
    if (g_exp_supported != -1) return g_exp_supported;
    g_exp_supported = 0;
    if (!build_supported()) return 0;
    for (unsigned i = 0; i < sizeof g_exp_fps / sizeof g_exp_fps[0]; i++)
        if (!mem_eq((const uint8_t *)(uintptr_t)g_exp_fps[i].va, g_exp_fps[i].bytes, g_exp_fps[i].n)) return 0;
    if (!mem_eq((const uint8_t *)(uintptr_t)g_exp_sites[0].va, g_exp_sites[0].orig, 2)) return 0;
    if (!mem_eq((const uint8_t *)(uintptr_t)g_exp_sites[1].va, g_exp_site1_orig, 5)) return 0;
    g_exp_supported = 1;
    return 1;
}

/* ---- ride list from Lua (one-way channel) -------------------------------------------------- */
#define EXP_MAX_IDS 64
static uint64_t g_exp_acc;
static uint64_t g_exp_pending[EXP_MAX_IDS];
static int g_exp_npending;
static uint64_t g_exp_active[EXP_MAX_IDS];
static volatile int g_exp_nactive;

/* ---- observe-only record of unrated destinations seen by the guest code -------------------- */
struct exp_seen { volatile uint64_t id; volatile uint32_t flags; volatile uint32_t count; };
static struct exp_seen g_exp_seen[EXP_MAX_IDS];
static volatile long g_exp_seen_overflow;

void exp_observe_record(const uint8_t *rec)
{
    uint64_t id = *(const uint64_t *)rec;
    uint32_t flags = *(const uint32_t *)(rec + 0x64);
    if (id == 0) return;
    unsigned h = (unsigned)(id * 0x9e3779b97f4a7c15ULL >> 58);       /* 0..63 */
    for (int probe = 0; probe < EXP_MAX_IDS; probe++) {
        struct exp_seen *e = &g_exp_seen[(h + probe) % EXP_MAX_IDS];
        uint64_t cur = e->id;
        if (cur == id) { e->flags = flags; __sync_fetch_and_add(&e->count, 1); return; }
        if (cur == 0 && __sync_bool_compare_and_swap(&e->id, 0, id)) { e->flags = flags; e->count = 1; return; }
        if (e->id == id) { __sync_fetch_and_add(&e->count, 1); return; }
    }
    g_exp_seen_overflow = 1;
}

/* exp_observe: replaces `mov eax,[r12+0x64]`. Performs that read; if the rated bits (0xD) are
 * not all set, records the destination. Preserves every register except eax/rax, like the
 * original instruction. Reached through a `jmp [rip+0]` stub, so no register is used to get here. */
void exp_observe(void);
__asm__(
    ".intel_syntax noprefix\n"
    ".text\n"
    ".globl exp_observe\n"
    "exp_observe:\n"
    "    mov eax, dword ptr [r12+0x64]\n"
    "    push r11\n"
    "    mov r11d, eax\n"
    "    and r11d, 0xd\n"
    "    cmp r11d, 0xd\n"
    "    je 1f\n"
    "    push rcx\n"
    "    push rdx\n"
    "    push r8\n"
    "    push r9\n"
    "    push r10\n"
    "    sub rsp, 0x88\n"
    "    movdqu xmmword ptr [rsp+0x20], xmm0\n"
    "    movdqu xmmword ptr [rsp+0x30], xmm1\n"
    "    movdqu xmmword ptr [rsp+0x40], xmm2\n"
    "    movdqu xmmword ptr [rsp+0x50], xmm3\n"
    "    movdqu xmmword ptr [rsp+0x60], xmm4\n"
    "    movdqu xmmword ptr [rsp+0x70], xmm5\n"
    "    mov rcx, r12\n"
    "    call exp_observe_record\n"
    "    movdqu xmm0, xmmword ptr [rsp+0x20]\n"
    "    movdqu xmm1, xmmword ptr [rsp+0x30]\n"
    "    movdqu xmm2, xmmword ptr [rsp+0x40]\n"
    "    movdqu xmm3, xmmword ptr [rsp+0x50]\n"
    "    movdqu xmm4, xmmword ptr [rsp+0x60]\n"
    "    movdqu xmm5, xmmword ptr [rsp+0x70]\n"
    "    add rsp, 0x88\n"
    "    pop r10\n"
    "    pop r9\n"
    "    pop r8\n"
    "    pop rdx\n"
    "    pop rcx\n"
    "    mov eax, dword ptr [r12+0x64]\n"
    "1:\n"
    "    pop r11\n"
    "    ret\n"
    ".att_syntax prefix\n");

/* Allocate the stub page within +-2 GB of the evaluator and compute the call bytes. */
static int exp_prepare_stub(void)
{
    if (g_exp_stub) return 1;
    const uint64_t site = 0x1406a051bULL;
    for (uint64_t a = 0x147800000ULL; a < 0x1c0000000ULL && !g_exp_stub; a += 0x10000)
        g_exp_stub = (uint8_t *)VirtualAlloc((void *)(uintptr_t)a, 0x1000, 0x3000, PAGE_EXECUTE_READWRITE);
    for (uint64_t a = 0x13f000000ULL; a > 0xc8000000ULL && !g_exp_stub; a -= 0x10000)
        g_exp_stub = (uint8_t *)VirtualAlloc((void *)(uintptr_t)a, 0x1000, 0x3000, PAGE_EXECUTE_READWRITE);
    if (!g_exp_stub) { log_line("experimental: could not allocate stub near the game image"); return 0; }
    int64_t rel = (int64_t)(uintptr_t)g_exp_stub - (int64_t)(site + 5);
    if (rel > 0x7fffffffLL || rel < -0x80000000LL) { log_line("experimental: stub out of range"); g_exp_stub = 0; return 0; }
    uint64_t target = (uint64_t)(uintptr_t)&exp_observe;
    g_exp_stub[0] = 0xff; g_exp_stub[1] = 0x25; g_exp_stub[2] = g_exp_stub[3] = g_exp_stub[4] = g_exp_stub[5] = 0;
    for (int i = 0; i < 8; i++) g_exp_stub[6 + i] = (uint8_t)(target >> (8 * i));
    FlushInstructionCache(GetCurrentProcess(), g_exp_stub, 16);
    g_exp_site1_patch[0] = 0xe8;
    for (int i = 0; i < 4; i++) g_exp_site1_patch[1 + i] = (uint8_t)((uint64_t)rel >> (8 * i));
    return 1;
}

/* 0 off, 1 on, -1 mixed/unknown */
static int exp_code_state(void)
{
    const uint8_t *p0 = (const uint8_t *)(uintptr_t)g_exp_sites[0].va;
    const uint8_t *p1 = (const uint8_t *)(uintptr_t)g_exp_sites[1].va;
    int off0 = mem_eq(p0, g_exp_sites[0].orig, 2), on0 = mem_eq(p0, g_exp_sites[0].patch, 2);
    int off1 = mem_eq(p1, g_exp_site1_orig, 5), on1 = g_exp_stub && mem_eq(p1, g_exp_site1_patch, 5);
    if (off0 && off1) return 0;
    if (on0 && on1) return 1;
    return -1;
}

static int exp_set(int on)
{
    if (!exp_supported()) { log_line("experimental: unexpected game code - not applied"); return ST_UNSUPPORTED; }
    if (on && !exp_prepare_stub()) return ST_PROTECT_FAIL;
    int cur = exp_code_state();
    if (cur == on) return on ? ST_ON : ST_OFF;
    if (cur < 0) { log_line("experimental: code in an unexpected state - not changed"); return ST_RACE; }
    const uint8_t *from[2] = {on ? g_exp_sites[0].orig : g_exp_sites[0].patch, on ? g_exp_site1_orig : g_exp_site1_patch};
    const uint8_t *to[2] = {on ? g_exp_sites[0].patch : g_exp_sites[0].orig, on ? g_exp_site1_patch : g_exp_site1_orig};
    const int n[2] = {2, 5};
    int done = 0, err = 0;
    for (; done < 2; done++) {
        int r = swap_bytes(g_exp_sites[done].va, n[done], from[done], to[done]);
        if (r != 1) { err = r < 0 ? ST_PROTECT_FAIL : ST_RACE; break; }
    }
    if (err) {
        for (int i = done - 1; i >= 0; i--) swap_bytes(g_exp_sites[i].va, n[i], to[i], from[i]);
        log_line("experimental: change failed and was rolled back");
        return err;
    }
    g_exp_state = on;
    log_line(on ? "experimental: ON (untested/unfinished rides may open; guest code observed, not changed)"
                : "experimental: OFF (original code restored)");
    return on ? ST_ON : ST_OFF;
}

/* ---- logging helpers (no C runtime) ------------------------------------------------------ */
static char *fmt_u64(char *p, uint64_t v)
{
    char t[24]; int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *p++ = t[--n];
    return p;
}
static char *fmt_str(char *p, const char *s) { while (*s) *p++ = *s++; return p; }

/* ---- Lua entry points --------------------------------------------------------------------- */
__declspec(dllexport) int irs_exp_enable(void *L) { (void)L; return exp_set(1); }
__declspec(dllexport) int irs_exp_disable(void *L) { (void)L; return exp_set(0); }
__declspec(dllexport) int irs_exp_status(void *L)
{
    (void)L;
    if (!exp_supported()) return ST_UNSUPPORTED;
    int st = exp_code_state();
    return st < 0 ? ST_RACE : (st ? ST_ON : ST_OFF);
}

/* channel: begin, then for each id: bits MSB first via bit0/bit1, then push; finally commit */
__declspec(dllexport) int irs_exp_begin(void *L) { (void)L; g_exp_acc = 0; g_exp_npending = 0; return 1; }
__declspec(dllexport) int irs_exp_bit0(void *L) { (void)L; g_exp_acc <<= 1; return 1; }
__declspec(dllexport) int irs_exp_bit1(void *L) { (void)L; g_exp_acc = (g_exp_acc << 1) | 1; return 1; }
__declspec(dllexport) int irs_exp_push(void *L)
{
    (void)L;
    if (g_exp_npending < EXP_MAX_IDS) g_exp_pending[g_exp_npending++] = g_exp_acc;
    g_exp_acc = 0;
    return 1;
}
__declspec(dllexport) int irs_exp_commit(void *L)
{
    (void)L;
    int changed = g_exp_npending != g_exp_nactive;
    for (int i = 0; !changed && i < g_exp_npending; i++) changed = g_exp_pending[i] != g_exp_active[i];
    for (int i = 0; i < g_exp_npending; i++) g_exp_active[i] = g_exp_pending[i];
    g_exp_nactive = g_exp_npending;
    if (changed) {
        char buf[64 + EXP_MAX_IDS * 22], *p = buf;
        p = fmt_str(p, "experimental: untested rides from Lua (station ids):");
        for (int i = 0; i < g_exp_nactive; i++) { *p++ = ' '; p = fmt_u64(p, g_exp_active[i]); }
        *p = 0;
        log_line(buf);
    }
    return 1;
}

/* Writes the unrated destinations seen by the guest code since the last call to the log, marking
 * those that match an id received from Lua. Returns 1 if any matched, 2 otherwise. */
__declspec(dllexport) int irs_exp_report(void *L)
{
    (void)L;
    int matched = 0, any = 0;
    char buf[96 + EXP_MAX_IDS * 40], *p = buf;
    p = fmt_str(p, "experimental: guest code skipped unrated destinations (id flags count):");
    for (int i = 0; i < EXP_MAX_IDS; i++) {
        struct exp_seen *e = &g_exp_seen[i];
        uint64_t id = e->id;
        if (!id) continue;
        uint32_t c = __sync_lock_test_and_set(&e->count, 0);
        if (!c) continue;
        int m = 0;
        for (int k = 0; k < g_exp_nactive; k++) m |= g_exp_active[k] == id;
        matched |= m; any = 1;
        *p++ = ' '; p = fmt_u64(p, id); *p++ = ':'; p = fmt_u64(p, e->flags); *p++ = ':'; p = fmt_u64(p, c);
        if (m) p = fmt_str(p, "(=ride from Lua)");
    }
    if (g_exp_seen_overflow) p = fmt_str(p, " [table full]");
    *p = 0;
    if (any) log_line(buf);
    return matched ? 1 : 2;
}
