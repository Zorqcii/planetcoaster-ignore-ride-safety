/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Zorqcii
 *
 * PROTOTYPE (0.2.0-proto.1): launch ONE crashed rider into the game's guest physics. Research only.
 * Off by default, behind its own option, and only while the experimental option is on. Included at
 * the end of irs_experimental.c (after irs_diag.c).
 *
 * Flow, one launch at most per crash:
 *  1. The diagnostic purge hook sees the destroyed-vehicle listener purge riders (caller 0x14081bb0f)
 *     and counts a crash (g_pt_crashes).
 *  2. The scripts pick one rider from their rider list taken before the crash and check, with public
 *     guest functions, that the guest still exists, is no longer on the ride, and that its group is not
 *     on a ride, queueing, in physics, trapped or leaving the park. They then arm the helper with that id.
 *  3. On the next tick, at the START of the guest-physics update 0x14067de10 (its own thread and
 *     object, before the update body runs), the helper checks the physics object (same object for
 *     >= 60 ticks, required fields present), that the guest is not already in its physics guest map
 *     and that the chosen group key is unused (read-only lookups, the game's own hash and layout),
 *     and that the arming is at most 250 ms old. Only then it calls the game's physics start routine
 *     0x14067d450(system, &guest, &{key, 0, 0, 0, 0}, 0): no position or velocity is set.
 *  4. For 180 s it records when the guest leaves the physics guest map and when the group key
 *     disappears; the scripts report the group's behaviour and IncidentEnded messages. The log is
 *     written from the script thread only.
 * Unticking stops further launches and removes the update hook; a guest already launched is left
 * to the game.
 */

#define PT_START_VA 0x14067d450ULL
typedef uint8_t (*pt_start_fn)(void *sys, const uint64_t *guest, const void *rec, uint64_t unused);

static struct xsite g_pt_site = {0x14067de10ULL, 5, {0x48, 0x8b, 0xc4, 0x55, 0x53}, {0}};
static const struct fp g_pt_fps[] = {
    /* guest-physics update after its entry: push r14 ; lea rbp,[rax-0x998] ; sub rsp,0xa80 ; ... mov r14,rcx */
    {0x14067de15ULL, 24, {0x41, 0x56, 0x48, 0x8d, 0xa8, 0x68, 0xf6, 0xff, 0xff, 0x48, 0x81, 0xec, 0x80, 0x0a, 0x00, 0x00, 0x0f, 0x29, 0x78, 0x98, 0x4c, 0x8b, 0xf1, 0x48}},
    /* update: mov rax,[rcx+0x218] */
    {0x14067de58ULL, 7, {0x48, 0x8b, 0x81, 0x18, 0x02, 0x00, 0x00}},
    /* start: add rcx,0x248 (group map) */
    {0x14067d46bULL, 7, {0x48, 0x81, 0xc1, 0x48, 0x02, 0x00, 0x00}},
    /* start: lea rcx,[rbp+0x228] (guest map) */
    {0x14067d47dULL, 7, {0x48, 0x8d, 0x8d, 0x28, 0x02, 0x00, 0x00}},
    /* start: mov rcx,[rbp+0x198] */
    {0x14067d4f4ULL, 7, {0x48, 0x8b, 0x8d, 0x98, 0x01, 0x00, 0x00}},
    /* start: mov rax,[rbp+0x1a8] */
    {0x14067d532ULL, 7, {0x48, 0x8b, 0x85, 0xa8, 0x01, 0x00, 0x00}},
    /* start: inc qword [rbp+0x288] ; mov al,1 ; mov dword [rbp+0x290],5.0f */
    {0x14067d6f0ULL, 17, {0x48, 0xff, 0x85, 0x88, 0x02, 0x00, 0x00, 0xb0, 0x01, 0xc7, 0x85, 0x90, 0x02, 0x00, 0x00, 0x00, 0x00}},
    /* guest map lookup (re-implemented read-only in pt_guest_in_map) */
    {0x1400c6f70ULL, 24, {0x4c, 0x8b, 0x02, 0x48, 0x8b, 0xf1, 0x4d, 0x8b, 0xc8, 0x49, 0x8b, 0xc0, 0x48, 0xf7, 0xd0, 0x49, 0xc1, 0xe1, 0x12, 0x4c, 0x03, 0xc8, 0x4c, 0x8b}},
    {0x1400c6f88ULL, 24, {0xf2, 0x49, 0x8b, 0xc1, 0x33, 0xd2, 0x48, 0xc1, 0xe8, 0x1f, 0x49, 0x33, 0xc1, 0x48, 0x6b, 0xc8, 0x15, 0x48, 0x8b, 0xc1, 0x48, 0xc1, 0xe8, 0x0b}},
    {0x1400c6fa0ULL, 24, {0x48, 0x33, 0xc1, 0x48, 0x6b, 0xc8, 0x41, 0x48, 0x8b, 0xc1, 0x8b, 0xc9, 0x48, 0xc1, 0xe8, 0x16, 0x8b, 0xc0, 0x48, 0x33, 0xc1, 0x48, 0xf7, 0x76}},
    {0x1400c6fb8ULL, 24, {0x08, 0x48, 0x8b, 0x06, 0x48, 0x8d, 0x0c, 0xd0, 0x48, 0x8b, 0x04, 0xd0, 0x48, 0x8b, 0xf9, 0x48, 0x3b, 0xc1, 0x74, 0x19, 0x0f, 0x1f, 0x40, 0x00}},
    {0x1400c6fd0ULL, 21, {0x4c, 0x3b, 0x40, 0x08, 0x0f, 0x84, 0xe3, 0x00, 0x00, 0x00, 0x48, 0x8b, 0xf8, 0x48, 0x8b, 0x00, 0x48, 0x3b, 0xc1, 0x75, 0xeb}},
    /* group map lookup (re-implemented read-only in pt_key_in_map) */
    {0x1406e9120ULL, 24, {0x44, 0x8b, 0x02, 0x48, 0x8b, 0xf1, 0x41, 0x69, 0xc0, 0x01, 0x10, 0x00, 0x00, 0x4c, 0x8b, 0xf2, 0x33, 0xd2, 0x44, 0x8b, 0xc8, 0x41, 0xc1, 0xe9}},
    {0x1406e9138ULL, 24, {0x16, 0x44, 0x33, 0xc8, 0x41, 0x6b, 0xc1, 0x11, 0x44, 0x8b, 0xc8, 0x41, 0xc1, 0xe9, 0x09, 0x44, 0x33, 0xc8, 0x41, 0x69, 0xc1, 0x01, 0x04, 0x00}},
    {0x1406e9150ULL, 24, {0x00, 0x8b, 0xc8, 0xc1, 0xe9, 0x02, 0x33, 0xc8, 0x69, 0xc1, 0x81, 0x00, 0x00, 0x00, 0x8b, 0xc8, 0x48, 0xc1, 0xe8, 0x0c, 0x48, 0x33, 0xc1, 0x48}},
    {0x1406e9168ULL, 24, {0xf7, 0x76, 0x08, 0x48, 0x8b, 0x06, 0x48, 0x8d, 0x0c, 0xd0, 0x48, 0x8b, 0x04, 0xd0, 0x48, 0x8b, 0xf9, 0x48, 0x3b, 0xc1, 0x74, 0x17, 0x66, 0x90}},
    {0x1406e9180ULL, 21, {0x44, 0x3b, 0x40, 0x10, 0x0f, 0x84, 0xe3, 0x00, 0x00, 0x00, 0x48, 0x8b, 0xf8, 0x48, 0x8b, 0x00, 0x48, 0x3b, 0xc1, 0x75, 0xeb}},
};

static int g_pt_supported = -1;
static volatile int g_pt_state;            /* option on (hook installed) */
static long g_pt_crashes_polled;            /* script thread */

/* arming (script thread -> update thread) */
static volatile uint64_t g_pt_arm_guest;
static volatile uint64_t g_pt_arm_time;
static volatile long g_pt_armed;

/* physics object tracking (update thread) */
static uint64_t g_pt_sys;
static uint32_t g_pt_sys_ticks;

/* result of the last attempt (update thread writes, script thread logs) */
enum { PT_NONE = 0, PT_LAUNCHED = 1, PT_REFUSED = 3, PT_EXPIRED = 4 };
struct pt_result {
    volatile long seq; long logged;
    int code; const char *reason;
    uint64_t guest, key, t, tid, sys_ticks, count_before, count_after; uint32_t timer_after; uint8_t ret;
};
static struct pt_result g_pt_res;

/* monitoring of the launched guest (update thread) */
static uint64_t g_pt_mon_guest, g_pt_mon_t0;
static uint32_t g_pt_mon_key;
static int g_pt_mon_active, g_pt_mon_in_guests, g_pt_mon_in_groups;
struct pt_trans { uint64_t t; uint8_t what, value; uint64_t count; uint32_t timer; };
#define PT_TRANS 16
static struct pt_trans g_pt_trans[PT_TRANS];
static volatile long g_pt_ntrans;
static long g_pt_ntrans_logged;

static int pt_supported(void)
{
    if (g_pt_supported != -1) return g_pt_supported;
    g_pt_supported = 0;
    if (!exp_supported()) return 0;
    for (unsigned i = 0; i < sizeof g_pt_fps / sizeof g_pt_fps[0]; i++)
        if (!mem_eq((const uint8_t *)(uintptr_t)g_pt_fps[i].va, g_pt_fps[i].bytes, g_pt_fps[i].n)) return 0;
    if (!mem_eq((const uint8_t *)(uintptr_t)g_pt_site.va, g_pt_site.orig, g_pt_site.n) &&
        !(g_exp_stub && mem_eq((const uint8_t *)(uintptr_t)g_pt_site.va, g_pt_site.patch, g_pt_site.n))) return 0;
    g_pt_supported = 1;
    return 1;
}

/* Read-only lookups with the game's own hash functions and node layouts (fingerprinted above). */
static int pt_guest_in_map(uint64_t sys, uint64_t guest)
{
    const uint64_t *buckets = *(const uint64_t *const *)(uintptr_t)(sys + 0x228);
    uint64_t nb = *(const uint64_t *)(uintptr_t)(sys + 0x230);
    if (!buckets || !nb) return -1;
    uint64_t h = (guest << 18) + ~guest;
    h ^= h >> 31; h *= 21; h ^= h >> 11; h *= 65;
    uint64_t idx = ((uint64_t)(uint32_t)(h >> 22) ^ (uint64_t)(uint32_t)h) % nb;
    const uint64_t *slot = &buckets[idx];
    const uint64_t *node = (const uint64_t *)(uintptr_t)*slot;
    for (int guard = 0; node && node != slot && guard < 100000; guard++) {
        if (node[1] == guest) return 1;
        node = (const uint64_t *)(uintptr_t)node[0];
    }
    return node ? 0 : -1;
}

static int pt_key_in_map(uint64_t sys, uint32_t key)
{
    const uint64_t *buckets = *(const uint64_t *const *)(uintptr_t)(sys + 0x248);
    uint64_t nb = *(const uint64_t *)(uintptr_t)(sys + 0x250);
    if (!buckets || !nb) return -1;
    uint32_t a = key * 0x1001u;
    a ^= a >> 22; a *= 0x11u; a ^= a >> 9; a *= 0x401u; a ^= a >> 2; a *= 0x81u;
    uint64_t idx = ((uint64_t)(a >> 12) ^ (uint64_t)a) % nb;
    const uint64_t *slot = &buckets[idx];
    const uint64_t *node = (const uint64_t *)(uintptr_t)*slot;
    for (int guard = 0; node && node != slot && guard < 100000; guard++) {
        if (*(const uint32_t *)((const uint8_t *)node + 0x10) == key) return 1;
        node = (const uint64_t *)(uintptr_t)node[0];
    }
    return node ? 0 : -1;
}

static void pt_trans(uint64_t sys, uint8_t what, uint8_t value)
{
    long n = g_pt_ntrans;
    if (n >= PT_TRANS) return;
    struct pt_trans *t = &g_pt_trans[n];
    t->t = GetTickCount64(); t->what = what; t->value = value;
    t->count = *(const uint64_t *)(uintptr_t)(sys + 0x288);
    t->timer = *(const uint32_t *)(uintptr_t)(sys + 0x290);
    __sync_synchronize();
    g_pt_ntrans = n + 1;
}

static void pt_refuse(const char *reason, uint64_t guest)
{
    g_pt_res.code = PT_REFUSED; g_pt_res.reason = reason; g_pt_res.guest = guest;
    g_pt_res.t = GetTickCount64(); g_pt_res.tid = GetCurrentThreadId(); g_pt_res.sys_ticks = g_pt_sys_ticks;
    __sync_synchronize();
    __sync_fetch_and_add(&g_pt_res.seq, 1);
}

/* Body of the update-entry hook (regs: [3] = rcx = guest-physics system object). */
void pt_update(const uint64_t *r)
{
    uint64_t sys = r[3];
    if (!sys) return;
    if (sys == g_pt_sys) { if (g_pt_sys_ticks < 0xffffffffu) g_pt_sys_ticks++; }
    else { g_pt_sys = sys; g_pt_sys_ticks = 1; }

    if (g_pt_mon_active) {
        uint64_t now = GetTickCount64();
        int ig = pt_guest_in_map(sys, g_pt_mon_guest), ik = pt_key_in_map(sys, g_pt_mon_key);
        if (ig != g_pt_mon_in_guests) { g_pt_mon_in_guests = ig; pt_trans(sys, 1, (uint8_t)ig); }
        if (ik != g_pt_mon_in_groups) { g_pt_mon_in_groups = ik; pt_trans(sys, 2, (uint8_t)ik); }
        if (now - g_pt_mon_t0 > 180000) { g_pt_mon_active = 0; pt_trans(sys, 3, 0); }
    }

    if (!g_pt_armed || !g_pt_state) return;
    uint64_t guest = g_pt_arm_guest, armed_at = g_pt_arm_time;
    __sync_lock_test_and_set(&g_pt_armed, 0);
    uint64_t now = GetTickCount64();
    if (now - armed_at > 250) {
        g_pt_res.code = PT_EXPIRED; g_pt_res.reason = "arming older than 250 ms"; g_pt_res.guest = guest; g_pt_res.t = now;
        __sync_synchronize(); __sync_fetch_and_add(&g_pt_res.seq, 1);
        return;
    }
    if (g_pt_mon_active) { pt_refuse("previous launch still being monitored", guest); return; }
    if (g_pt_sys_ticks < 60) { pt_refuse("physics object not yet stable for 60 ticks", guest); return; }
    if (!*(const uint64_t *)(uintptr_t)(sys + 0x198) || !*(const uint64_t *)(uintptr_t)(sys + 0x1a0) ||
        !*(const uint64_t *)(uintptr_t)(sys + 0x1a8) || !*(const uint64_t *)(uintptr_t)(sys + 0x218)) {
        pt_refuse("physics object is missing a required field", guest); return;
    }
    int in_map = pt_guest_in_map(sys, guest);
    if (in_map < 0) { pt_refuse("physics guest map unreadable", guest); return; }
    if (in_map) { pt_refuse("guest is already in the physics guest map", guest); return; }
    uint32_t key = 0;
    for (uint32_t k = 0x7a490000u + (uint32_t)(g_pt_crashes & 0xfff) * 16u, i = 0; i < 16; i++, k++) {
        int used = pt_key_in_map(sys, k);
        if (used < 0) { pt_refuse("physics group map unreadable", guest); return; }
        if (!used) { key = k; break; }
    }
    if (!key) { pt_refuse("no unused group key", guest); return; }

    static uint64_t s_guest;
    static struct { uint32_t key; float v[4]; } s_rec;
    s_guest = guest; s_rec.key = key; s_rec.v[0] = s_rec.v[1] = s_rec.v[2] = s_rec.v[3] = 0.0f;
    g_pt_res.count_before = *(const uint64_t *)(uintptr_t)(sys + 0x288);
    uint8_t ret = ((pt_start_fn)(uintptr_t)PT_START_VA)((void *)(uintptr_t)sys, &s_guest, &s_rec, 0);
    g_pt_res.count_after = *(const uint64_t *)(uintptr_t)(sys + 0x288);
    g_pt_res.timer_after = *(const uint32_t *)(uintptr_t)(sys + 0x290);
    g_pt_res.ret = ret; g_pt_res.code = PT_LAUNCHED; g_pt_res.reason = "launched"; g_pt_res.guest = guest; g_pt_res.key = key;
    g_pt_res.t = GetTickCount64(); g_pt_res.tid = GetCurrentThreadId(); g_pt_res.sys_ticks = g_pt_sys_ticks;
    g_pt_mon_guest = guest; g_pt_mon_key = key; g_pt_mon_t0 = g_pt_res.t;
    g_pt_mon_in_guests = pt_guest_in_map(sys, guest); g_pt_mon_in_groups = pt_key_in_map(sys, key);
    g_pt_ntrans = 0; g_pt_ntrans_logged = 0;
    g_pt_mon_active = 1;
    __sync_synchronize();
    __sync_fetch_and_add(&g_pt_res.seq, 1);
}

void *g_pt_tramp_update;
void pt_hook_update(void);
DX_ENTRY_WRAPPER(pt_hook_update, pt_update, g_pt_tramp_update);

/* Stub page: +0x200 stub9 -> pt_hook_update ; +0x220 trampoline H (5 original bytes, jmp 0x14067de15). */
static int pt_prepare(void)
{
    if (!g_exp_stub) return 0;
    uint8_t *page = g_exp_stub;
    if (g_pt_site.patch[0] == 0xe9) return 1;
    put_abs_jmp(page + 0x200, (uint64_t)(uintptr_t)&pt_hook_update);
    for (int i = 0; i < 5; i++) page[0x220 + i] = g_pt_site.orig[i];
    put_abs_jmp(page + 0x225, g_pt_site.va + 5);
    FlushInstructionCache(GetCurrentProcess(), page + 0x200, 0x40);
    int ok = 1;
    uint32_t r = rel32(g_pt_site.va, (uint64_t)(uintptr_t)(page + 0x200), &ok);
    if (!ok) return 0;
    g_pt_tramp_update = page + 0x220;
    for (int i = 0; i < 4; i++) g_pt_site.patch[1 + i] = (uint8_t)(r >> (8 * i));
    g_pt_site.patch[0] = 0xe9;
    return 1;
}

static int pt_code_state(void)
{
    const uint8_t *p = (const uint8_t *)(uintptr_t)g_pt_site.va;
    if (mem_eq(p, g_pt_site.orig, 5)) return 0;
    if (g_pt_site.patch[0] == 0xe9 && mem_eq(p, g_pt_site.patch, 5)) return 1;
    return -1;
}

static int pt_set(int on)
{
    if (!pt_supported()) { if (on) log_line("proto: unexpected game code - not applied"); return ST_UNSUPPORTED; }
    if (on && !g_exp_state) { log_line("proto: needs the experimental option on - not applied"); return ST_UNSUPPORTED; }
    if (on && !pt_prepare()) { log_line("proto: could not prepare the update hook"); return ST_PROTECT_FAIL; }
    if (!on) { g_pt_state = 0; __sync_lock_test_and_set(&g_pt_armed, 0); }
    int cur = pt_code_state();
    if (cur == on) { g_pt_state = on; return on ? ST_ON : ST_OFF; }
    if (cur < 0) { log_line("proto: update entry in an unexpected state - not changed"); return ST_RACE; }
    int r = swap_bytes(g_pt_site.va, 5, on ? g_pt_site.orig : g_pt_site.patch, on ? g_pt_site.patch : g_pt_site.orig);
    if (r != 1) { log_line("proto: change failed"); return r < 0 ? ST_PROTECT_FAIL : ST_RACE; }
    if (on) {
        g_pt_crashes_polled = g_pt_crashes;     /* only crashes from now on */
        g_pt_state = 1;
        log_line(pt_code_state() == 1 ? "proto: ON - update hook at 0x14067de10 installed and read back OK; one rider per crash"
                                      : "proto: ON but READ-BACK FAILED");
    } else {
        log_line("proto: OFF (update hook removed; a guest already launched is left to the game)");
    }
    return on ? ST_ON : ST_OFF;
}

/* ---- logging of results and transitions (script thread) -------------------------------- */
static void pt_report(void)
{
    if (g_pt_res.seq != g_pt_res.logged) {
        g_pt_res.logged = g_pt_res.seq;
        __sync_synchronize();
        char b[320], *p = b;
        p = fmt_str(p, "proto t="); p = fmt_u64(p, g_pt_res.t - g_dx_t0); p = fmt_str(p, "ms ");
        if (g_pt_res.code == PT_LAUNCHED) {
            p = fmt_str(p, "LAUNCH guest "); p = fmt_u64(p, g_pt_res.guest);
            p = fmt_str(p, " group key "); p = fmt_hex(p, g_pt_res.key);
            p = fmt_str(p, " start returned "); p = fmt_u64(p, g_pt_res.ret);
            p = fmt_str(p, ", incident counter "); p = fmt_u64(p, g_pt_res.count_before); p = fmt_str(p, " -> "); p = fmt_u64(p, g_pt_res.count_after);
            p = fmt_str(p, ", timer bits "); p = fmt_hex(p, g_pt_res.timer_after);
            p = fmt_str(p, ", in physics guest map "); p = fmt_u64(p, (uint64_t)(g_pt_mon_in_guests & 0xff));
            p = fmt_str(p, ", group present "); p = fmt_u64(p, (uint64_t)(g_pt_mon_in_groups & 0xff));
            p = fmt_str(p, ", thread "); p = fmt_u64(p, g_pt_res.tid);
        } else {
            p = fmt_str(p, g_pt_res.code == PT_EXPIRED ? "NOT launched (expired): " : "NOT launched: ");
            p = fmt_str(p, g_pt_res.reason ? g_pt_res.reason : "?");
            p = fmt_str(p, ", guest "); p = fmt_u64(p, g_pt_res.guest);
        }
        *p = 0; log_line(b);
    }
    long n = g_pt_ntrans;
    __sync_synchronize();
    for (; g_pt_ntrans_logged < n && g_pt_ntrans_logged < PT_TRANS; g_pt_ntrans_logged++) {
        const struct pt_trans *t = &g_pt_trans[g_pt_ntrans_logged];
        char b[200], *p = b;
        p = fmt_str(p, "proto +"); p = fmt_u64(p, t->t - g_pt_mon_t0); p = fmt_str(p, "ms after launch: ");
        if (t->what == 1) p = fmt_str(p, t->value == 1 ? "guest is in the physics guest map" : t->value == 0 ? "guest LEFT the physics guest map" : "guest map unreadable");
        else if (t->what == 2) p = fmt_str(p, t->value == 1 ? "group present" : t->value == 0 ? "group REMOVED" : "group map unreadable");
        else p = fmt_str(p, "monitoring ended (180 s)");
        p = fmt_str(p, " (incident counter "); p = fmt_u64(p, t->count); p = fmt_str(p, ", timer bits "); p = fmt_hex(p, t->timer); *p++ = ')';
        *p = 0; log_line(b);
    }
}

/* ---- Lua entry points --------------------------------------------------------------------- */
__declspec(dllexport) int irs_pt_enable(void *L) { (void)L; return pt_set(1); }
__declspec(dllexport) int irs_pt_disable(void *L) { (void)L; return pt_set(0); }
__declspec(dllexport) int irs_pt_status(void *L)
{
    (void)L;
    if (!pt_supported()) return ST_UNSUPPORTED;
    int st = pt_code_state();
    return st < 0 ? ST_RACE : (st ? ST_ON : ST_OFF);
}
/* 1 = a crash purge happened since the last poll (counted once), 2 = none */
__declspec(dllexport) int irs_pt_poll(void *L)
{
    (void)L;
    long c = g_pt_crashes;
    if (c != g_pt_crashes_polled) { g_pt_crashes_polled = c; return 1; }
    return 2;
}
/* arm with the guest id sent through the channel (begin, bits, push) */
__declspec(dllexport) int irs_pt_arm(void *L)
{
    (void)L;
    if (!g_pt_state || g_exp_npending < 1) return 2;
    g_pt_arm_guest = g_exp_pending[0];
    g_pt_arm_time = GetTickCount64();
    __sync_synchronize();
    __sync_lock_test_and_set(&g_pt_armed, 1);
    char b[96], *p = b;
    p = fmt_str(p, "proto t="); p = fmt_u64(p, g_pt_arm_time - g_dx_t0); p = fmt_str(p, "ms armed with guest "); p = fmt_u64(p, g_pt_arm_guest);
    *p = 0; log_line(b);
    return 1;
}
/* last attempt: 1 launched, 2 pending, 3 refused, 4 expired, 5 nothing yet; also writes new log lines */
__declspec(dllexport) int irs_pt_result(void *L)
{
    (void)L;
    pt_report();
    if (g_pt_armed) return 2;
    if (!g_pt_res.seq) return 5;
    return g_pt_res.code == PT_LAUNCHED ? 1 : g_pt_res.code == PT_EXPIRED ? 4 : 3;
}
/* notes from the scripts: channel values [kind, a, b] */
__declspec(dllexport) int irs_pt_note(void *L)
{
    (void)L;
    static const char *beh[] = {"(none)", "Physics", "Trapped", "Navigating", "Idle", "Lost", "OnRide", "Queueing", "AtSecurityGuard", "other"};
    static const char *why[] = {"eligible", "guest not found", "guest not in its group", "still on the ride", "group busy", "leaving the park"};
    if (g_exp_npending < 1) return 2;
    uint64_t kind = g_exp_pending[0], a = g_exp_npending > 1 ? g_exp_pending[1] : 0, c = g_exp_npending > 2 ? g_exp_pending[2] : 0;
    char b[200], *p = b;
    p = fmt_str(p, "proto t="); p = fmt_u64(p, GetTickCount64() - g_dx_t0); p = fmt_str(p, "ms ");
    switch (kind) {
    case 1: p = fmt_str(p, "candidate guest "); p = fmt_u64(p, a); p = fmt_str(p, ": "); p = fmt_str(p, why[c < 6 ? c : 0]);
            if (g_exp_npending > 3) { p = fmt_str(p, " (group behaviour "); p = fmt_str(p, beh[g_exp_pending[3] < 10 ? g_exp_pending[3] : 9]); *p++ = ')'; }
            break;
    case 2: p = fmt_str(p, "launched guest's group behaviour: "); p = fmt_str(p, beh[a < 10 ? a : 9]); break;
    case 3: p = fmt_str(p, "GuestPhysicsIncidentEnded message, guests involved "); p = fmt_u64(p, a); break;
    case 4: p = fmt_str(p, "launched guest no longer found by the scripts"); break;
    case 5: p = fmt_str(p, "trapped (SOS) guests now "); p = fmt_u64(p, a); break;
    case 6: p = fmt_str(p, "crash "); p = fmt_u64(p, a); p = fmt_str(p, ": no eligible rider within 10 s - no launch"); break;
    case 7: p = fmt_str(p, "crash "); p = fmt_u64(p, a); p = fmt_str(p, ": rider list before the crash had "); p = fmt_u64(p, c); p = fmt_str(p, " riders"); break;
    case 8: p = fmt_str(p, "launched guest found again by the scripts"); break;
    case 9: p = fmt_str(p, "crash "); p = fmt_u64(p, a); p = fmt_str(p, ": previous launch still being monitored - no launch"); break;
    default: p = fmt_str(p, "note "); p = fmt_u64(p, kind); break;
    }
    *p = 0; log_line(b);
    return 1;
}
