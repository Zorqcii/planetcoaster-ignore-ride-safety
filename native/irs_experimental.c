/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Zorqcii
 *
 * EXPERIMENTAL: unfinished / untested rides  (included at the end of irs_patch.c)
 *
 * Stage 2 (0.2.0-exp.2): guests see assumed ratings for listed rides (stage 1 was observe-only). Nothing here is active unless the player ticks the
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
 *  3) Guest hook: in the guest ride evaluator, `mov eax,[r12+0x64]` at 0x1406a051b (the read of
 *     the "destination has ratings" flags) is replaced by a call to exp_observe, which performs the
 *     same read and, when the destination is unrated, records its id. Stage 1 confirmed in game that
 *     this id is the station id the Lua side reports. If the id is on the list from Lua (open,
 *     untested rides only), the evaluator is given a private per-thread COPY of the guest's
 *     assessment record with assumed ratings (Excitement 8, Fear 8, Nausea 4, prestige 300) and the
 *     rated bits set; r12 then points at the copy for the rest of that candidate's evaluation.
 *     The game's record is never written. [rbp-0x40], from which the evaluator takes the chosen
 *     destination it returns, still holds the original record, so the copy never escapes. Only eax
 *     (as before) and, for listed rides, r12 are changed.
 */

__declspec(dllimport) void *__stdcall VirtualAlloc(void *, uintptr_t, DWORD, DWORD);
__declspec(dllimport) DWORD __stdcall TlsAlloc(void);
__declspec(dllimport) void *__stdcall TlsGetValue(DWORD);
__declspec(dllimport) BOOL __stdcall TlsSetValue(DWORD, void *);
__declspec(dllimport) HANDLE __stdcall GetProcessHeap(void);
__declspec(dllimport) void *__stdcall HeapAlloc(HANDLE, DWORD, uintptr_t);

/* No C runtime: required symbol when floating point is used. */
int _fltused = 0;

#define EXP_ASSUMED_EXCITEMENT 8.0f
#define EXP_ASSUMED_FEAR 8.0f
#define EXP_ASSUMED_NAUSEA 4.0f
#define EXP_ASSUMED_PRESTIGE 300.0f

/* ---- patch sites ------------------------------------------------------------------------- */
/* Each site lies within one aligned 8-byte word. Patch bytes of sites 1..3 are computed at runtime
 * (they jump/call into the stub page). */
struct xsite { uint64_t va; int n; uint8_t orig[8]; uint8_t patch[8]; };
static struct xsite g_exp_sites[] = {
    /* 0: open gate: je -> 2-byte nop */
    {0x14053812eULL, 2, {0x74, 0x0c}, {0x66, 0x90}},
    /* 1: evaluator: mov eax,[r12+0x64] -> call stub0 (exp_observe) */
    {0x1406a051bULL, 5, {0x41, 0x8b, 0x44, 0x24, 0x64}, {0}},
    /* 2: join check A entry (0x1406a2990): push rbp(40 55) ; push rsi ; push rdi ; push r14(41 56) -> jmp stub1 ; nop */
    {0x1406a2990ULL, 6, {0x40, 0x55, 0x56, 0x57, 0x41, 0x56}, {0}},
    /* 3: join check B entry (0x1406a3470): mov [rsp+0x20],r9 -> jmp stub2 */
    {0x1406a3470ULL, 5, {0x4c, 0x89, 0x4c, 0x24, 0x20}, {0}},
    /* 4: train-removed handler 0x1405d3fb0: call IsClosed (0x140537170) -> call stub3 (exp_isclosed) */
    {0x1405d40aaULL, 5, {0xe8, 0xc1, 0x30, 0xf6, 0xff}, {0}},
    /* 5: close-request function 0x140537510 entry: mov [rsp+0x18],rsi -> jmp stub4 (log only) */
    {0x140537510ULL, 5, {0x48, 0x89, 0x74, 0x24, 0x18}, {0}},
};
#define EXP_NSITES 6

static const struct fp g_exp_fps[] = {
    /* open gate: mov ecx,[rdi+0xc] ; test al,al ; jne ; test ecx,ecx ; (je) ; mov eax,[rdi+0x10] */
    {0x140538125ULL, 9, {0x8b, 0x4f, 0x0c, 0x84, 0xc0, 0x75, 0x10, 0x85, 0xc9}},
    {0x140538130ULL, 3, {0x8b, 0x47, 0x10}},
    /* evaluator candidate loop: mov r12,[r14] ; mov [rbp-0x40],r12 ; cmp qword [r12+0x10],0 ; mov rsi,[r12] */
    {0x1406a0500ULL, 17, {0x4d, 0x8b, 0x26, 0x4c, 0x89, 0x65, 0xc0, 0x49, 0x83, 0x7c, 0x24, 0x10, 0x00,
                          0x49, 0x8b, 0x34, 0x24}},
    /* (mov eax,[r12+0x64]) ; and eax,0xd ; cmp al,0xd */
    {0x1406a0520ULL, 5, {0x83, 0xe0, 0x0d, 0x3c, 0x0d}},
    /* join check A: (prologue) push r15 ; lea rbp,[rsp-0x27] ; sub rsp,0xd0 ... record = [rbp+0x77] (5th arg) */
    {0x1406a2996ULL, 10, {0x41, 0x57, 0x48, 0x8d, 0x6c, 0x24, 0xd9, 0x48, 0x81, 0xec}},
    {0x1406a29dbULL, 13, {0x4c, 0x8b, 0x65, 0x77, 0x41, 0x8b, 0x44, 0x24, 0x64, 0x83, 0xe0, 0x0d, 0x3c}},
    /* join check B: (prologue) push rbp ; push rbx ; push rsi ; push rdi ; push r14 ; push r15 ; lea rbp,[rsp-0x208] */
    {0x1406a3475ULL, 11, {0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0xac}},
    /* train-removed handler: mov rcx,[r14+0x2a0] ; (call IsClosed) ; test al,al ; jne +0x43 ; ... call close */
    {0x1405d40a3ULL, 7, {0x49, 0x8b, 0x8e, 0xa0, 0x02, 0x00, 0x00}},
    {0x1405d40afULL, 4, {0x84, 0xc0, 0x75, 0x43}},
    /* close-request function: (mov [rsp+0x18],rsi) ; push rdi ; sub rsp,0x20 ; mov r8,[rdx] ; mov rdi,rcx */
    {0x140537515ULL, 11, {0x57, 0x48, 0x83, 0xec, 0x20, 0x4c, 0x8b, 0x02, 0x48, 0x8b, 0xf9}},
    /* join check B: mov rax,[rbp+0x260] ; mov eax,[rax+0x64] ; and eax,0xd ; cmp al,0xd  (record = 5th arg) */
    {0x1406a35b2ULL, 15, {0x48, 0x8b, 0x85, 0x60, 0x02, 0x00, 0x00, 0x8b, 0x40, 0x64, 0x83, 0xe0, 0x0d, 0x3c, 0x0d}},
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
    for (int i = 0; i < EXP_NSITES; i++) {
        const struct xsite *x = &g_exp_sites[i];
        if ((x->va & 7) + (uint64_t)x->n > 8) return 0;
        if (!mem_eq((const uint8_t *)(uintptr_t)x->va, x->orig, x->n)) return 0;
    }
    g_exp_supported = 1;
    return 1;
}

/* ---- ride list from Lua (one-way channel) -------------------------------------------------- */
#define EXP_MAX_IDS 64
static uint64_t g_exp_acc;
static uint64_t g_exp_pending[EXP_MAX_IDS];
static int g_exp_npending;
/* active list, double-buffered: Lua (one thread) fills the inactive buffer, then flips g_exp_cur;
 * guest threads read the current buffer. */
static uint64_t g_exp_lists[2][EXP_MAX_IDS];
static volatile int g_exp_counts[2];
static volatile int g_exp_cur;
static uint64_t g_exp_active[EXP_MAX_IDS];   /* copy for logging/matching on the Lua thread */
static int g_exp_nactive;
static DWORD g_exp_tls = 0xffffffffu;
static volatile long g_exp_shadow_logged;
static volatile long g_exp_join_logged;

static int exp_listed(uint64_t id)
{
    int cur = g_exp_cur;
    __sync_synchronize();
    int n = g_exp_counts[cur];
    for (int i = 0; i < n && i < EXP_MAX_IDS; i++)
        if (g_exp_lists[cur][i] == id) return 1;
    return 0;
}

/* ---- observe-only record of unrated destinations seen by the guest code -------------------- */
struct exp_seen { volatile uint64_t id; volatile uint32_t flags; volatile uint32_t count; };
static struct exp_seen g_exp_seen[EXP_MAX_IDS];
static volatile long g_exp_seen_overflow;

static void exp_record_seen(uint64_t id, uint32_t flags)
{
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

/* Called from exp_observe for an unrated destination record; returns a shadow copy or 0. */
void *exp_observe_record(const uint8_t *rec)
{
    uint64_t id = *(const uint64_t *)rec;
    uint32_t flags = *(const uint32_t *)(rec + 0x64);
    if (id == 0) return 0;
    exp_record_seen(id, flags);
    if (!g_exp_state || !exp_listed(id) || g_exp_tls == 0xffffffffu) return 0;
    uint8_t *buf = (uint8_t *)TlsGetValue(g_exp_tls);
    if (!buf) {
        buf = (uint8_t *)HeapAlloc(GetProcessHeap(), 0, 0x100);
        if (!buf) return 0;
        TlsSetValue(g_exp_tls, buf);
    }
    for (int i = 0; i < 0x80; i++) buf[i] = rec[i];
    uint32_t fl = *(uint32_t *)(buf + 0x64);
    if (!(fl & 1)) {
        *(float *)(buf + 0x40) = EXP_ASSUMED_EXCITEMENT;
        *(float *)(buf + 0x44) = EXP_ASSUMED_FEAR;
        *(float *)(buf + 0x48) = EXP_ASSUMED_NAUSEA;
    }
    if ((fl & 0xc) != 0xc) *(float *)(buf + 0x58) = EXP_ASSUMED_PRESTIGE;
    *(uint32_t *)(buf + 0x64) = fl | 0xd;
    if (!g_exp_shadow_logged) {
        g_exp_shadow_logged = 1;
        log_line("experimental: guests now see assumed ratings for a listed ride");
    }
    return buf;
}

/* exp_observe: replaces `mov eax,[r12+0x64]`. Performs that read; if the rated bits (0xD) are
 * not all set, records the destination and, for listed rides, switches r12 to a shadow copy.
 * Preserves every other register. Reached through a `jmp [rip+0]` stub (no register used). */
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
    "    test rax, rax\n"
    "    jz 2f\n"
    "    mov r12, rax\n"
    "2:\n"
    "    mov eax, dword ptr [r12+0x64]\n"
    "1:\n"
    "    pop r11\n"
    "    ret\n"
    ".att_syntax prefix\n");

/* ---- join checks (guest at the ride entrance) --------------------------------------------- */
/* Called by the join wrappers with the 5th argument (guest assessment record) of 0x1406a2990 /
 * 0x1406a3470. For an unrated record of a listed (open, untested) ride, returns a per-thread copy
 * with the rated bits set; otherwise 0. These functions only test the rated bits and read
 * non-rating fields (+0x08, +0x4c, +0x50, +0x5d), and do not keep the pointer. */
void *exp_join_record(const uint8_t *rec)
{
    if (!g_exp_state || !rec || g_exp_tls == 0xffffffffu) return 0;
    uint32_t flags = *(const uint32_t *)(rec + 0x64);
    if ((flags & 0xd) == 0xd) return 0;
    uint64_t id = *(const uint64_t *)rec;
    if (!exp_listed(id)) return 0;
    uint8_t *buf = (uint8_t *)TlsGetValue(g_exp_tls);
    if (!buf) {
        buf = (uint8_t *)HeapAlloc(GetProcessHeap(), 0, 0x100);
        if (!buf) return 0;
        TlsSetValue(g_exp_tls, buf);
    }
    uint8_t *copy = buf + 0x80;      /* separate from the evaluator's copy at buf+0 */
    for (int i = 0; i < 0x80; i++) copy[i] = rec[i];
    *(uint32_t *)(copy + 0x64) = flags | 0xd;
    if (!g_exp_join_logged) {
        g_exp_join_logged = 1;
        log_line("experimental: guests now pass the entrance check for a listed ride");
    }
    return copy;
}

/* Wrapper entered by `jmp` from the patched function entry, so the stack is exactly as on entry:
 * [rsp] = return address, [rsp+0x28] = 5th argument. Preserves rcx, rdx, r8, r9 and xmm0-xmm3
 * (the argument registers); uses only rax and r11 (volatile, not arguments). Ends by jumping to
 * the trampoline that runs the original prologue bytes and continues in the game function. */
#define EXP_JOIN_WRAPPER(name, tramp) \
    __asm__(".intel_syntax noprefix\n.text\n.globl " #name "\n" #name ":\n" \
            "    mov rax, qword ptr [rsp+0x28]\n" \
            "    test rax, rax\n" \
            "    jz 1f\n" \
            "    push rcx\n    push rdx\n    push r8\n    push r9\n" \
            "    sub rsp, 0x68\n" \
            "    movdqu xmmword ptr [rsp+0x20], xmm0\n    movdqu xmmword ptr [rsp+0x30], xmm1\n" \
            "    movdqu xmmword ptr [rsp+0x40], xmm2\n    movdqu xmmword ptr [rsp+0x50], xmm3\n" \
            "    mov rcx, rax\n" \
            "    call exp_join_record\n" \
            "    movdqu xmm0, xmmword ptr [rsp+0x20]\n    movdqu xmm1, xmmword ptr [rsp+0x30]\n" \
            "    movdqu xmm2, xmmword ptr [rsp+0x40]\n    movdqu xmm3, xmmword ptr [rsp+0x50]\n" \
            "    add rsp, 0x68\n" \
            "    pop r9\n    pop r8\n    pop rdx\n    pop rcx\n" \
            "    test rax, rax\n" \
            "    jz 1f\n" \
            "    mov qword ptr [rsp+0x28], rax\n" \
            "1:\n" \
            "    jmp qword ptr [rip + " #tramp "]\n" \
            ".att_syntax prefix\n")
void *g_exp_join_tramp_a;   /* address of trampoline A (original prologue + jump back) */
void *g_exp_join_tramp_b;
void exp_join_a(void);
void exp_join_b(void);
EXP_JOIN_WRAPPER(exp_join_a, g_exp_join_tramp_a);
EXP_JOIN_WRAPPER(exp_join_b, g_exp_join_tramp_b);

/* ---- stage 4: keep listed rides open when their train is removed (crash) ------------------ */
typedef uint8_t (*isclosed_fn)(void *mgr, uint64_t *id);
#define GAME_ISCLOSED ((isclosed_fn)(uintptr_t)0x140537170ULL)
static volatile long g_exp_keepopen_count;
static volatile long g_exp_keepopen_logged;

/* Replaces the `call IsClosed` in the train-removed handler. For a listed (open, untested) ride it
 * reports "closed", so the handler takes the game's own already-closed path and does not close
 * the ride (nor run its close follow-up). Everything else is answered by the game's IsClosed. */
uint8_t exp_isclosed(void *mgr, uint64_t *id)
{
    if (g_exp_state && id && exp_listed(*id)) {
        __sync_fetch_and_add(&g_exp_keepopen_count, 1);
        return 1;
    }
    return GAME_ISCLOSED(mgr, id);
}

/* Log-only: close requests for listed rides, by calling address (to identify any other path). */
struct exp_closer { volatile uint64_t caller; volatile uint32_t count; };
static struct exp_closer g_exp_closers[16];

void exp_close_seen(const uint64_t *id, uint64_t caller)
{
    if (!g_exp_state || !id || !exp_listed(*id)) return;
    for (int i = 0; i < 16; i++) {
        uint64_t c = g_exp_closers[i].caller;
        if (c == caller) { __sync_fetch_and_add(&g_exp_closers[i].count, 1); return; }
        if (c == 0 && __sync_bool_compare_and_swap(&g_exp_closers[i].caller, 0, caller)) {
            __sync_fetch_and_add(&g_exp_closers[i].count, 1);
            return;
        }
    }
}

/* Entered by `jmp` from the close function's entry: rcx = manager, rdx = &station id, [rsp] = return
 * address. Records the request, then runs the original first instruction from a trampoline and
 * continues in the game function. Never blocks a close. */
void *g_exp_close_tramp;
void exp_close_hook(void);
__asm__(".intel_syntax noprefix\n.text\n.globl exp_close_hook\nexp_close_hook:\n"
        "    push rcx\n    push rdx\n    push r8\n    push r9\n"
        "    sub rsp, 0x28\n"
        "    mov rcx, rdx\n"
        "    mov rdx, qword ptr [rsp+0x48]\n"
        "    call exp_close_seen\n"
        "    add rsp, 0x28\n"
        "    pop r9\n    pop r8\n    pop rdx\n    pop rcx\n"
        "    jmp qword ptr [rip + g_exp_close_tramp]\n"
        ".att_syntax prefix\n");

/* Writes `jmp qword ptr [rip+0] ; dq target` at p (14 bytes). */
static void put_abs_jmp(uint8_t *p, uint64_t target)
{
    p[0] = 0xff; p[1] = 0x25; p[2] = p[3] = p[4] = p[5] = 0;
    for (int i = 0; i < 8; i++) p[6 + i] = (uint8_t)(target >> (8 * i));
}

/* rel32 from (site + 5) to target, or 0 with *ok = 0 if out of range */
static uint32_t rel32(uint64_t site, uint64_t target, int *ok)
{
    int64_t r = (int64_t)target - (int64_t)(site + 5);
    if (r > 0x7fffffffLL || r < -0x80000000LL) { *ok = 0; return 0; }
    return (uint32_t)r;
}

/* Allocate the stub page within +-2 GB of the game code and compute the patch bytes.
 * Layout: +0x00 stub0 -> exp_observe ; +0x20 stub1 -> exp_join_a ; +0x40 stub2 -> exp_join_b ;
 *         +0x60 trampoline A: original 6 prologue bytes of 0x1406a2990, then jmp 0x1406a2996 ;
 *         +0x80 trampoline B: original 5 prologue bytes of 0x1406a3470, then jmp 0x1406a3475 ;
 *         +0xa0 stub3 -> exp_isclosed ; +0xc0 stub4 -> exp_close_hook ;
 *         +0xe0 trampoline C: original 5 bytes of 0x140537510, then jmp 0x140537515. */
static int exp_prepare_stub(void)
{
    if (g_exp_stub) return 1;
    uint8_t *page = 0;
    for (uint64_t a = 0x147800000ULL; a < 0x1c0000000ULL && !page; a += 0x10000)
        page = (uint8_t *)VirtualAlloc((void *)(uintptr_t)a, 0x1000, 0x3000, PAGE_EXECUTE_READWRITE);
    for (uint64_t a = 0x13f000000ULL; a > 0xc8000000ULL && !page; a -= 0x10000)
        page = (uint8_t *)VirtualAlloc((void *)(uintptr_t)a, 0x1000, 0x3000, PAGE_EXECUTE_READWRITE);
    if (!page) { log_line("experimental: could not allocate stub near the game image"); return 0; }
    put_abs_jmp(page + 0x00, (uint64_t)(uintptr_t)&exp_observe);
    put_abs_jmp(page + 0x20, (uint64_t)(uintptr_t)&exp_join_a);
    put_abs_jmp(page + 0x40, (uint64_t)(uintptr_t)&exp_join_b);
    for (int i = 0; i < 6; i++) page[0x60 + i] = g_exp_sites[2].orig[i];
    put_abs_jmp(page + 0x66, 0x1406a2996ULL);
    for (int i = 0; i < 5; i++) page[0x80 + i] = g_exp_sites[3].orig[i];
    put_abs_jmp(page + 0x85, 0x1406a3475ULL);
    put_abs_jmp(page + 0xa0, (uint64_t)(uintptr_t)&exp_isclosed);
    put_abs_jmp(page + 0xc0, (uint64_t)(uintptr_t)&exp_close_hook);
    for (int i = 0; i < 5; i++) page[0xe0 + i] = g_exp_sites[5].orig[i];
    put_abs_jmp(page + 0xe5, 0x140537515ULL);
    FlushInstructionCache(GetCurrentProcess(), page, 0x100);
    int ok = 1;
    uint32_t r;
    r = rel32(g_exp_sites[1].va, (uint64_t)(uintptr_t)(page + 0x00), &ok);   /* call stub0 */
    g_exp_sites[1].patch[0] = 0xe8;
    for (int i = 0; i < 4; i++) g_exp_sites[1].patch[1 + i] = (uint8_t)(r >> (8 * i));
    r = rel32(g_exp_sites[2].va, (uint64_t)(uintptr_t)(page + 0x20), &ok);   /* jmp stub1 ; nop */
    g_exp_sites[2].patch[0] = 0xe9;
    for (int i = 0; i < 4; i++) g_exp_sites[2].patch[1 + i] = (uint8_t)(r >> (8 * i));
    g_exp_sites[2].patch[5] = 0x90;
    r = rel32(g_exp_sites[3].va, (uint64_t)(uintptr_t)(page + 0x40), &ok);   /* jmp stub2 */
    g_exp_sites[3].patch[0] = 0xe9;
    for (int i = 0; i < 4; i++) g_exp_sites[3].patch[1 + i] = (uint8_t)(r >> (8 * i));
    r = rel32(g_exp_sites[4].va, (uint64_t)(uintptr_t)(page + 0xa0), &ok);   /* call stub3 */
    g_exp_sites[4].patch[0] = 0xe8;
    for (int i = 0; i < 4; i++) g_exp_sites[4].patch[1 + i] = (uint8_t)(r >> (8 * i));
    r = rel32(g_exp_sites[5].va, (uint64_t)(uintptr_t)(page + 0xc0), &ok);   /* jmp stub4 */
    g_exp_sites[5].patch[0] = 0xe9;
    for (int i = 0; i < 4; i++) g_exp_sites[5].patch[1 + i] = (uint8_t)(r >> (8 * i));
    g_exp_close_tramp = page + 0xe0;
    if (!ok) { log_line("experimental: stub out of range"); return 0; }
    g_exp_join_tramp_a = page + 0x60;
    g_exp_join_tramp_b = page + 0x80;
    g_exp_stub = page;
    return 1;
}

/* 0 off, 1 on, -1 mixed/unknown */
static int exp_code_state(void)
{
    int off = 0, on = 0;
    for (int i = 0; i < EXP_NSITES; i++) {
        const struct xsite *x = &g_exp_sites[i];
        const uint8_t *p = (const uint8_t *)(uintptr_t)x->va;
        if (mem_eq(p, x->orig, x->n)) off++;
        else if (g_exp_stub && mem_eq(p, x->patch, x->n)) on++;
    }
    if (off == EXP_NSITES) return 0;
    if (on == EXP_NSITES) return 1;
    return -1;
}

static int exp_set(int on)
{
    if (!exp_supported()) { log_line("experimental: unexpected game code - not applied"); return ST_UNSUPPORTED; }
    if (on && !exp_prepare_stub()) return ST_PROTECT_FAIL;
    if (on && g_exp_tls == 0xffffffffu) g_exp_tls = TlsAlloc();
    if (!on) {                       /* stop handing out copies before restoring the code */
        g_exp_state = 0;
        g_exp_counts[0] = g_exp_counts[1] = 0;
        __sync_synchronize();
    }
    int cur = exp_code_state();
    if (cur == on) { g_exp_state = on; return on ? ST_ON : ST_OFF; }
    if (cur < 0) { log_line("experimental: code in an unexpected state - not changed"); return ST_RACE; }
    int done = 0, err = 0;
    for (; done < EXP_NSITES; done++) {
        const struct xsite *x = &g_exp_sites[done];
        int r = swap_bytes(x->va, x->n, on ? x->orig : x->patch, on ? x->patch : x->orig);
        if (r != 1) { err = r < 0 ? ST_PROTECT_FAIL : ST_RACE; break; }
    }
    if (err) {
        for (int i = done - 1; i >= 0; i--) {
            const struct xsite *x = &g_exp_sites[i];
            swap_bytes(x->va, x->n, on ? x->patch : x->orig, on ? x->orig : x->patch);
        }
        log_line("experimental: change failed and was rolled back");
        return err;
    }
    g_exp_state = on;
    log_line(on ? "experimental: ON (untested/unfinished rides may open; listed open rides are treated as rated by guests)"
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
    int next = 1 - g_exp_cur;
    for (int i = 0; i < g_exp_npending; i++) g_exp_lists[next][i] = g_exp_pending[i];
    g_exp_counts[next] = g_exp_npending;
    __sync_synchronize();
    g_exp_cur = next;
    if (changed) {
        char buf[64 + EXP_MAX_IDS * 22], *p = buf;
        p = fmt_str(p, "experimental: open untested rides from Lua (station ids):");
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
    long kept = __sync_lock_test_and_set(&g_exp_keepopen_count, 0);
    if (kept) {
        char b2[96], *q = b2;
        q = fmt_str(q, "experimental: kept a listed ride open after its train was removed, times: ");
        q = fmt_u64(q, (uint64_t)kept); *q = 0;
        log_line(b2);
    }
    for (int i = 0; i < 16; i++) {
        uint32_t c = __sync_lock_test_and_set(&g_exp_closers[i].count, 0);
        if (!c) continue;
        char b3[128], *q = b3;
        q = fmt_str(q, "experimental: close requested for a listed ride from game code at 0x");
        uint64_t v = g_exp_closers[i].caller;
        for (int k = 60; k >= 0; k -= 4) *q++ = "0123456789abcdef"[(v >> k) & 15];
        q = fmt_str(q, ", times: "); q = fmt_u64(q, c); *q = 0;
        log_line(b3);
    }
    return matched ? 1 : 2;
}
