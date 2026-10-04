/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Zorqcii
 *
 * DIAGNOSTIC round 1 (crash-rider physics): observation only. Included at the end of irs_experimental.c.
 *
 * While the experimental option is on, three game routines are entered through unfiltered entry hooks.
 * Every call is recorded, whatever the ids:
 *   0x1405d3fb0  train-removed handler    (rcx = ride system, rdx = &train)
 *   0x14081a0a0  purge riders of rides    (rides:PurgeAllRideGuests; rdx = id vector: count +0x10, data +0x18)
 *   0x14067d450  guest-physics start      (rdx = &guest id, r8 = &{group key, ...})
 * plus a timestamped anchor whenever the crash handler asks to close a ride (existing stage-5 hook).
 * Each hook reads only fields that the routine itself reads, records an event in a fixed ring buffer
 * and continues into the original code; no argument, register or game memory is changed.
 * The log file is written only from the Lua thread (irs_dx_report), never from a hook.
 */

__declspec(dllimport) uint64_t __stdcall GetTickCount64(void);
__declspec(dllimport) DWORD __stdcall GetCurrentThreadId(void);

#define DX_BUILD 1                 /* returned by irs_dx_build: helper is diagnostic build 1 */
enum { DX_CRASHCLOSE = 1, DX_TRAIN = 2, DX_PURGE = 3, DX_PHYSICS = 4, DX_KINDS = 5 };

struct dx_ev {
    volatile uint64_t seq;         /* index + 1 once written */
    uint32_t kind, tid;
    uint64_t t, ret, a, b, c;
};
#define DX_RING 256                /* power of two */
static struct dx_ev g_dx_ring[DX_RING];
static volatile uint64_t g_dx_head;
static uint64_t g_dx_tail;         /* Lua thread only */
static volatile long g_dx_counts[DX_KINDS];
static long g_dx_counts_logged[DX_KINDS];
static uint64_t g_dx_t0;
static long g_dx_printed;          /* total event lines written (budget) */
#define DX_PRINT_BUDGET 3000
#define DX_PRINT_PER_REPORT 48

static void dx_event(uint32_t kind, uint64_t ret, uint64_t a, uint64_t b, uint64_t c)
{
    if (kind >= DX_KINDS) return;
    __sync_fetch_and_add(&g_dx_counts[kind], 1);
    uint64_t i = __sync_fetch_and_add(&g_dx_head, 1);
    struct dx_ev *e = &g_dx_ring[i & (DX_RING - 1)];
    e->seq = 0;
    __sync_synchronize();
    e->kind = kind;
    e->tid = GetCurrentThreadId();
    e->t = GetTickCount64();
    e->ret = ret; e->a = a; e->b = b; e->c = c;
    __sync_synchronize();
    e->seq = i + 1;
}

/* ---- riders of open untested rides (from Lua, same one-way channel) ------------------------ */
#define DX_MAX_RIDERS 128
static uint64_t g_dx_riders[2][DX_MAX_RIDERS];  /* published: union of the last two non-empty snapshots */
static volatile int g_dx_nriders[2];
static volatile int g_dx_rcur;
static uint64_t g_dx_prev[DX_MAX_RIDERS];        /* previous non-empty snapshot (Lua thread) */
static int g_dx_nprev;
static int g_dx_have_riders;
static int g_dx_last_count = -1;

/* 1 = a recent rider of an open untested ride, 2 = not in the rider lists, 0 = no rider list received yet */
static uint64_t dx_rider_class(uint64_t guest)
{
    int cur = g_dx_rcur;
    __sync_synchronize();
    int n = g_dx_nriders[cur];
    if (!g_dx_have_riders) return 0;
    for (int i = 0; i < n && i < DX_MAX_RIDERS; i++)
        if (g_dx_riders[cur][i] == guest) return 1;
    return 2;
}

/* ---- hook bodies (regs: [0]=r9 [1]=r8 [2]=rdx [3]=rcx [4]=return address) --------------- */
void dx_train(const uint64_t *r)
{
    uint64_t ecx = r[3], edx = r[2], id160 = 0, id2e0 = 0, flags = r[0] & 0xff;
    flags <<= 16;
    if (ecx) flags |= (uint64_t)*(const uint8_t *)(uintptr_t)(ecx + 0x438) | ((uint64_t)*(const uint8_t *)(uintptr_t)(ecx + 0x439) << 8);
    if (edx) {
        uint64_t train = *(const uint64_t *)(uintptr_t)edx;
        if (train) {
            id160 = *(const uint64_t *)(uintptr_t)(train + 0x160);
            id2e0 = *(const uint64_t *)(uintptr_t)(train + 0x2e0);
        }
    }
    dx_event(DX_TRAIN, r[4], id160, id2e0, flags);
}

void dx_purge(const uint64_t *r)
{
    uint64_t v = r[2], n = 0, first = 0, second = 0;
    if (v) {
        n = *(const uint64_t *)(uintptr_t)(v + 0x10);
        const uint64_t *data = (const uint64_t *)(uintptr_t)*(const uint64_t *)(uintptr_t)(v + 0x18);
        if (data && n > 0) first = data[0];
        if (data && n > 1) second = data[1];
    }
    dx_event(DX_PURGE, r[4], n, first, second);
}

void dx_physics(const uint64_t *r)
{
    uint64_t guest = 0, key = 0;
    if (r[2]) guest = *(const uint64_t *)(uintptr_t)r[2];
    if (r[1]) key = *(const uint32_t *)(uintptr_t)r[1];
    dx_event(DX_PHYSICS, r[4], guest, key, dx_rider_class(guest));
}

/* Entered by `jmp` from a hooked function entry (stack exactly as on entry). Saves the argument
 * registers and xmm0-xmm3, calls the C body with a pointer to the saved registers and the return
 * address, restores everything and jumps to the trampoline (original first bytes + jump back).
 * Uses rax and r11 only as scratch; neither carries an argument at a function entry. */
#define DX_ENTRY_WRAPPER(name, body, tramp) \
    __asm__(".intel_syntax noprefix\n.text\n.globl " #name "\n" #name ":\n" \
            "    push rcx\n    push rdx\n    push r8\n    push r9\n" \
            "    sub rsp, 0x68\n" \
            "    movdqu xmmword ptr [rsp+0x20], xmm0\n    movdqu xmmword ptr [rsp+0x30], xmm1\n" \
            "    movdqu xmmword ptr [rsp+0x40], xmm2\n    movdqu xmmword ptr [rsp+0x50], xmm3\n" \
            "    lea rcx, [rsp+0x68]\n" \
            "    call " #body "\n" \
            "    movdqu xmm0, xmmword ptr [rsp+0x20]\n    movdqu xmm1, xmmword ptr [rsp+0x30]\n" \
            "    movdqu xmm2, xmmword ptr [rsp+0x40]\n    movdqu xmm3, xmmword ptr [rsp+0x50]\n" \
            "    add rsp, 0x68\n" \
            "    pop r9\n    pop r8\n    pop rdx\n    pop rcx\n" \
            "    jmp qword ptr [rip + " #tramp "]\n" \
            ".att_syntax prefix\n")
void *g_dx_tramp_train;
void *g_dx_tramp_purge;
void *g_dx_tramp_physics;
DX_ENTRY_WRAPPER(dx_hook_train, dx_train, g_dx_tramp_train);
DX_ENTRY_WRAPPER(dx_hook_purge, dx_purge, g_dx_tramp_purge);
DX_ENTRY_WRAPPER(dx_hook_physics, dx_physics, g_dx_tramp_physics);

/* ---- reporting (Lua thread) --------------------------------------------------------------- */
static char *fmt_hex(char *p, uint64_t v)
{
    *p++ = '0'; *p++ = 'x';
    int k = 60;
    while (k > 0 && ((v >> k) & 15) == 0) k -= 4;
    for (; k >= 0; k -= 4) *p++ = "0123456789abcdef"[(v >> k) & 15];
    return p;
}

static const char *dx_caller_name(uint64_t ret)
{
    switch (ret) {
    case 0x1405d4041ULL: return "train-removed handler, 1st purge";
    case 0x1405d4088ULL: return "train-removed handler, 2nd purge";
    case 0x14046c09eULL: return "script rides:PurgeAllRideGuests (positive control)";
    case 0x14081bb14ULL: return "unexplained caller A";
    case 0x140856a8bULL: return "unexplained caller B";
    case 0x1405cee38ULL: return "caller 0x1405cede0";
    case 0x1405d16fdULL: return "caller 0x1405d0c50";
    case 0x1405d78eeULL: return "caller 0x1405d7867";
    case 0x14408ee69ULL: return "protected decision code";
    case 0x1409a934bULL: return "crash handler";
    default: return "other";
    }
}

static void dx_line(const struct dx_ev *e, uint64_t idx)
{
    static const char *names[DX_KINDS] = {"?", "CRASH-CLOSE", "TRAIN-REMOVED", "PURGE", "PHYSICS-START"};
    char b[320], *p = b;
    p = fmt_str(p, "diag #"); p = fmt_u64(p, idx + 1);
    p = fmt_str(p, " t="); p = fmt_u64(p, e->t - g_dx_t0); p = fmt_str(p, "ms thread "); p = fmt_u64(p, e->tid);
    *p++ = ' '; p = fmt_str(p, names[e->kind < DX_KINDS ? e->kind : 0]);
    p = fmt_str(p, " from "); p = fmt_hex(p, e->ret); p = fmt_str(p, " ("); p = fmt_str(p, dx_caller_name(e->ret)); *p++ = ')';
    switch (e->kind) {
    case DX_CRASHCLOSE:
        p = fmt_str(p, " ride "); p = fmt_u64(p, e->a); p = fmt_str(p, e->b ? " listed (close skipped)" : " not listed");
        break;
    case DX_TRAIN:
        p = fmt_str(p, " train id@160 "); p = fmt_u64(p, e->a); p = fmt_str(p, " id@2e0 "); p = fmt_u64(p, e->b);
        p = fmt_str(p, " flags438/439 "); p = fmt_u64(p, e->c & 0xff); *p++ = '/'; p = fmt_u64(p, (e->c >> 8) & 0xff);
        p = fmt_str(p, ((e->c & 0xff) && ((e->c >> 8) & 0xff)) ? " (early exit: no purge)" : " (normal path)");
        p = fmt_str(p, " r9b "); p = fmt_u64(p, (e->c >> 16) & 0xff);
        break;
    case DX_PURGE:
        p = fmt_str(p, " ids "); p = fmt_u64(p, e->a);
        if (e->a > 0) { p = fmt_str(p, ": "); p = fmt_u64(p, e->b); }
        if (e->a > 1) { p = fmt_str(p, ", "); p = fmt_u64(p, e->c); }
        if (e->a > 2) p = fmt_str(p, ", ...");
        for (int k = 0; k < g_exp_nactive; k++)
            if (e->a > 0 && (g_exp_active[k] == e->b || (e->a > 1 && g_exp_active[k] == e->c))) { p = fmt_str(p, " (listed ride)"); break; }
        break;
    case DX_PHYSICS:
        p = fmt_str(p, " guest "); p = fmt_u64(p, e->a); p = fmt_str(p, " group key "); p = fmt_u64(p, e->b);
        p = fmt_str(p, e->c == 1 ? " = RIDER of an open untested ride" : e->c == 2 ? " = not in the rider lists" : " (no rider list yet)");
        break;
    }
    *p = 0;
    log_line(b);
}

static void dx_report(void)
{
    uint64_t head = g_dx_head;
    int printed = 0;
    uint64_t lost = 0, skipped = 0;
    if (head - g_dx_tail > DX_RING) { lost += head - g_dx_tail - DX_RING; g_dx_tail = head - DX_RING; }
    while (g_dx_tail < head) {
        uint64_t i = g_dx_tail;
        struct dx_ev *slot = &g_dx_ring[i & (DX_RING - 1)];
        uint64_t s1 = slot->seq;
        if (s1 == 0 || s1 < i + 1) break;              /* still being written: next report */
        __sync_synchronize();
        struct dx_ev e = *slot;
        __sync_synchronize();
        if (s1 != i + 1 || slot->seq != s1) { lost++; g_dx_tail++; continue; }   /* overwritten */
        if (printed < DX_PRINT_PER_REPORT && g_dx_printed < DX_PRINT_BUDGET) { dx_line(&e, i); printed++; g_dx_printed++; }
        else skipped++;
        g_dx_tail++;
    }
    if (lost || skipped) {
        char b[128], *p = b;
        p = fmt_str(p, "diag: events not printed: "); p = fmt_u64(p, skipped);
        p = fmt_str(p, ", lost (ring overrun): "); p = fmt_u64(p, lost);
        *p = 0; log_line(b);
    }
    int changed = 0;
    for (int k = 1; k < DX_KINDS; k++) changed |= g_dx_counts[k] != g_dx_counts_logged[k];
    if (changed) {
        char b[200], *p = b;
        p = fmt_str(p, "diag totals since load: crash-close "); p = fmt_u64(p, (uint64_t)g_dx_counts[DX_CRASHCLOSE]);
        p = fmt_str(p, ", train-removed "); p = fmt_u64(p, (uint64_t)g_dx_counts[DX_TRAIN]);
        p = fmt_str(p, ", purge "); p = fmt_u64(p, (uint64_t)g_dx_counts[DX_PURGE]);
        p = fmt_str(p, ", physics-start "); p = fmt_u64(p, (uint64_t)g_dx_counts[DX_PHYSICS]);
        *p = 0; log_line(b);
        for (int k = 1; k < DX_KINDS; k++) g_dx_counts_logged[k] = g_dx_counts[k];
    }
}

/* Called from exp_set after the experimental code was applied: reads every hook back. */
static void dx_log_installed(void)
{
    if (!g_dx_t0) g_dx_t0 = GetTickCount64();
    int ok = exp_code_state() == 1;
    log_line(ok ? "diag: observation hooks installed and read back OK: 0x1405d3fb0 train-removed, 0x14081a0a0 purge, "
                  "0x14067d450 physics-start; crash anchor 0x140815460 (from crash handler 0x1409a934b)"
                : "diag: READ-BACK FAILED - hooks not all in place");
}

/* ---- Lua entry points --------------------------------------------------------------------- */
__declspec(dllexport) int irs_dx_build(void *L) { (void)L; return DX_BUILD; }

/* riders: begin, bits, push per guest id (shared channel), then this instead of irs_exp_commit */
__declspec(dllexport) int irs_dx_riders_commit(void *L)
{
    (void)L;
    /* first value: marker (bit 62) carrying the number of riders the script found */
    if (g_exp_npending < 1 || (g_exp_pending[0] >> 62) != 1) return 2;
    int found = (int)(g_exp_pending[0] & 0xffffffffu);
    const uint64_t *ids = g_exp_pending + 1;
    int n = g_exp_npending - 1;
    if (n > DX_MAX_RIDERS) n = DX_MAX_RIDERS;
    if (found != g_dx_last_count) {
        char b[128], *p = b;
        p = fmt_str(p, "diag t="); p = fmt_u64(p, GetTickCount64() - g_dx_t0);
        p = fmt_str(p, "ms riders on open untested rides: "); p = fmt_u64(p, (uint64_t)found);
        p = fmt_str(p, " (ids received "); p = fmt_u64(p, (uint64_t)n); *p++ = ')';
        *p = 0; log_line(b);
        g_dx_last_count = found;
    }
    if (n == 0) return 1;                       /* keep the last two non-empty snapshots published */
    int next = 1 - g_dx_rcur, m = 0;
    for (int i = 0; i < n; i++) g_dx_riders[next][m++] = ids[i];
    for (int i = 0; i < g_dx_nprev && m < DX_MAX_RIDERS; i++) {
        int dup = 0;
        for (int k = 0; k < n && !dup; k++) dup = ids[k] == g_dx_prev[i];
        if (!dup) g_dx_riders[next][m++] = g_dx_prev[i];
    }
    g_dx_nriders[next] = m;
    __sync_synchronize();
    g_dx_rcur = next;
    g_dx_have_riders = 1;
    for (int i = 0; i < n; i++) g_dx_prev[i] = ids[i];
    g_dx_nprev = n;
    return 1;
}

__declspec(dllexport) int irs_dx_report(void *L) { (void)L; dx_report(); return 1; }
