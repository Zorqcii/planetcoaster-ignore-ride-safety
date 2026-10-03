/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Zorqcii
 *
 * Ignore Ride Safety - native helper for Planet Coaster (2016)
 * Supported executable: PlanetCoaster.exe, game build 1.13.3.88540, PE timestamp 0x663b9a50.
 * Loaded by the mod's Lua scripts with package.loadlib(). Everything it changes lives in memory
 * of the running game process; no file on disk is modified.
 *
 * The game's guest ride evaluator (at 0x1406a0020 in the supported build) scores a candidate ride.
 *
 * FEAR option ("Ignore ride safety concerns"):
 *     tooIntense       = ride.fear > guest.MaxRideIntensity      (seta al)
 *     notIntenseEnough = guest.MinRideIntensity > ride.fear      (seta bl / cl)
 *   A ride is refused with thought "Assessment_RideTooIntense" when tooIntense is set. Both
 *   `seta al` instructions (main and secondary assessment) become `mov al,0; nop`, so only
 *   "too intense" is suppressed. "Not intense enough" and the nausea factor are unchanged.
 *
 * NAUSEA option ("Ignore ride nausea"):
 *   With N = ride nausea rating, the ride's appeal is multiplied by a nausea factor:
 *     N <  guest comfortable limit                -> 1.0
 *     comfortable <= N < guest tolerable limit    -> fades from 1.0 to a tuning value
 *     tolerable   <= N < global hard cap          -> fades to 0
 *     N >= hard cap                               -> 0 (refused)
 *   The `jb` that enters this block becomes a short `jmp` to the block's existing exit, so the
 *   factor stays at its initial 1.0. The fear check is unchanged.
 *
 * This deliberately does NOT use the game's built-in "IgnoreFearAndNausea" cheat flag, which
 * suppresses "too intense" AND "not intense enough" AND softens nausea, and cannot be undone.
 *
 * Safety:
 *   - Before changing anything the helper checks the image base, the PE timestamp and exact
 *     bytes around every patch site. On any mismatch it changes nothing and reports
 *     "unsupported" (also written to IgnoreRideSafety.log next to the DLL).
 *   - Each patch fits inside one aligned 8-byte word and is swapped with a single atomic
 *     compare-and-exchange. If any site of an option cannot be changed, the sites already
 *     changed for that option are put back, so an option is never left half-applied.
 *   - Disabling an option writes the original bytes back.
 *
 * Exported functions are lua_CFunctions that never read or write the game's (customised) Lua
 * state: the caller passes N distinct values and the function returns k (1..N), which makes Lua
 * hand back the top k arguments, so the caller reads the status code as the first result.
 * Calling convention used by the mod:  local st = fn(6, 5, 4, 3, 2, 1)  ->  st == status
 */
#include <stdint.h>

typedef void *HANDLE;
typedef int BOOL;
typedef unsigned long DWORD;
typedef uint16_t WCHAR;

#define DLL_PROCESS_ATTACH 1
#define PAGE_EXECUTE_READWRITE 0x40
#define GENERIC_WRITE 0x40000000
#define FILE_SHARE_READ 1
#define OPEN_ALWAYS 4
#define FILE_END 2
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)

__declspec(dllimport) BOOL __stdcall VirtualProtect(void *, uintptr_t, DWORD, DWORD *);
__declspec(dllimport) BOOL __stdcall FlushInstructionCache(HANDLE, const void *, uintptr_t);
__declspec(dllimport) HANDLE __stdcall GetCurrentProcess(void);
__declspec(dllimport) HANDLE __stdcall GetModuleHandleW(const WCHAR *);
__declspec(dllimport) DWORD __stdcall GetModuleFileNameW(HANDLE, WCHAR *, DWORD);
__declspec(dllimport) HANDLE __stdcall CreateFileW(const WCHAR *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
__declspec(dllimport) DWORD __stdcall SetFilePointer(HANDLE, long, long *, DWORD);
__declspec(dllimport) BOOL __stdcall WriteFile(HANDLE, const void *, DWORD, DWORD *, void *);
__declspec(dllimport) BOOL __stdcall CloseHandle(HANDLE);

#define IRS_VERSION "0.2.0-exp.5 EXPERIMENTAL"

/* status codes returned to Lua */
enum {
    ST_ON = 1,            /* option active */
    ST_OFF = 2,           /* original game code in place */
    ST_UNSUPPORTED = 3,   /* game build does not match; nothing was changed */
    ST_PROTECT_FAIL = 4,  /* could not make code writable; nothing left changed */
    ST_RACE = 5,          /* code bytes not as expected (another mod?); nothing left changed */
};

struct site {
    uint64_t va;          /* address of the patched bytes (inside one aligned 8-byte word) */
    uint8_t n;
    uint8_t orig[4];
    uint8_t patch[4];
};

struct group {
    const char *on_msg;
    const char *off_msg;
    const struct site *sites;
    int nsites;
};

/* FEAR: mov al,0 ; nop  replaces  seta al (0f 97 c0) */
static const struct site g_fear_sites[] = {
    {0x1406a0a9dULL, 3, {0x0f, 0x97, 0xc0}, {0xb0, 0x00, 0x90}},  /* main assessment */
    {0x1406a1869ULL, 3, {0x0f, 0x97, 0xc0}, {0xb0, 0x00, 0x90}},  /* secondary assessment */
};
/* NAUSEA: jb rel32 (0f 82 ..) -> jmp short +0x7e (eb 7e) to the block exit at 0x1406a0b65 */
static const struct site g_nausea_sites[] = {
    {0x1406a0ae5ULL, 2, {0x0f, 0x82}, {0xeb, 0x7e}},
};

enum { G_FEAR = 0, G_NAUSEA = 1, G_COUNT = 2 };
static const struct group g_groups[G_COUNT] = {
    {"fear: enabled (guests ignore 'ride too intense')", "fear: disabled (original code restored)", g_fear_sites, 2},
    {"nausea: enabled (guests ignore ride nausea rating)", "nausea: disabled (original code restored)", g_nausea_sites, 1},
};

/* Fingerprints: exact bytes around each site in the supported executable. */
struct fp { uint64_t va; uint8_t n; uint8_t bytes[24]; };
static const struct fp g_fps[] = {
    /* fear: comiss xmm1,[rbx+0xb0] ... comiss xmm0,xmm1 ; seta bl ; test al,al */
    {0x1406a0a80ULL, 24, {0x0f, 0x2f, 0x8b, 0xb0, 0x00, 0x00, 0x00, 0xf3, 0x0f, 0x10, 0x83, 0xac,
                          0x00, 0x00, 0x00, 0xf3, 0x41, 0x0f, 0x10, 0x5c, 0x24, 0x40, 0xf3, 0x41}},
    {0x1406a0aa0ULL, 8, {0x0f, 0x2f, 0xc1, 0x0f, 0x97, 0xc3, 0x84, 0xc0}},
    {0x1406a1853ULL, 7, {0x0f, 0x2f, 0x8b, 0xb0, 0x00, 0x00, 0x00}},
    {0x1406a186cULL, 8, {0x0f, 0x2f, 0xc1, 0x0f, 0x97, 0xc1, 0x84, 0xc0}},
    /* nausea: movaps xmm6,xmm14 (factor = 1.0) */
    {0x1406a0ac0ULL, 4, {0x41, 0x0f, 0x28, 0xf6}},
    /* mov r9,[rsp+0x70] ; movss xmm1,[r9+0x134] ; comiss xmm2,xmm1 ; (jb) rel32 = 0x100 */
    {0x1406a0ad4ULL, 17, {0x4c, 0x8b, 0x4c, 0x24, 0x70, 0xf3, 0x41, 0x0f, 0x10, 0x89, 0x34, 0x01,
                          0x00, 0x00, 0x0f, 0x2f, 0xd1}},
    {0x1406a0ae7ULL, 4, {0x00, 0x01, 0x00, 0x00}},
    /* block exit: mov rbx,[rbp+0x3b0] ; mov rax,[rsp+0x70] */
    {0x1406a0b65ULL, 12, {0x48, 0x8b, 0x9d, 0xb0, 0x03, 0x00, 0x00, 0x48, 0x8b, 0x44, 0x24, 0x70}},
};
#define PE_TIMESTAMP 0x663b9a50u

static HANDLE g_self;
static int g_supported = -1;  /* -1 unknown, 0 no, 1 yes */

static int mem_eq(const uint8_t *a, const uint8_t *b, int n)
{
    for (int i = 0; i < n; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

/* ---- log file next to the DLL --------------------------------------------------------- */
static void log_line(const char *msg)
{
    WCHAR path[600];
    DWORD n = GetModuleFileNameW(g_self, path, 560);
    if (n == 0 || n >= 560) return;
    while (n > 0 && path[n - 1] != '\\' && path[n - 1] != '/') n--;
    static const char name[] = "IgnoreRideSafety.log";
    for (int i = 0; name[i]; i++) path[n++] = (WCHAR)name[i];
    path[n] = 0;
    HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, 0, OPEN_ALWAYS, 0x80, 0);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, 0, FILE_END);
    DWORD len = 0, w;
    while (msg[len]) len++;
    WriteFile(h, msg, len, &w, 0);
    WriteFile(h, "\r\n", 2, &w, 0);
    CloseHandle(h);
}

/* ---- build verification ----------------------------------------------------------------- */
static int group_code_state(const struct group *g);

static int build_supported(void)
{
    if (g_supported != -1) return g_supported;
    g_supported = 0;
    const uint8_t *base = (const uint8_t *)GetModuleHandleW(0);
    if ((uint64_t)(uintptr_t)base != 0x140000000ULL) return 0;
    int32_t e_lfanew = *(const int32_t *)(base + 0x3c);
    if (*(const uint32_t *)(base + e_lfanew) != 0x00004550u) return 0;          /* "PE\0\0" */
    if (*(const uint32_t *)(base + e_lfanew + 8) != PE_TIMESTAMP) return 0;
    for (unsigned i = 0; i < sizeof g_fps / sizeof g_fps[0]; i++)
        if (!mem_eq((const uint8_t *)(uintptr_t)g_fps[i].va, g_fps[i].bytes, g_fps[i].n)) return 0;
    for (int i = 0; i < G_COUNT; i++)
        if (group_code_state(&g_groups[i]) < 0) return 0;
    g_supported = 1;
    return 1;
}

/* Determine an option's state from the code bytes. Returns 0 off, 1 on, -1 mixed/unknown. */
static int group_code_state(const struct group *g)
{
    int on = 0, off = 0;
    for (int i = 0; i < g->nsites; i++) {
        const struct site *s = &g->sites[i];
        const uint8_t *p = (const uint8_t *)(uintptr_t)s->va;
        if (mem_eq(p, s->orig, s->n)) off++;
        else if (mem_eq(p, s->patch, s->n)) on++;
    }
    if (off == g->nsites) return 0;
    if (on == g->nsites) return 1;
    return -1;
}

/* Atomically swap n bytes at va (they lie inside one aligned 8-byte word).
 * Returns 1 on success, 0 if the bytes were not `from`, -1 if the page could not be made writable. */
static int swap_bytes(uint64_t va, int n, const uint8_t *from, const uint8_t *to)
{
    uint64_t q = va & ~7ULL;
    unsigned off = (unsigned)(va - q);
    if (off + (unsigned)n > 8) return 0;
    volatile uint64_t *qp = (volatile uint64_t *)(uintptr_t)q;
    DWORD old;
    if (!VirtualProtect((void *)(uintptr_t)q, 8, PAGE_EXECUTE_READWRITE, &old)) return -1;
    int ok = 0;
    for (int tries = 0; tries < 8 && !ok; tries++) {
        uint64_t cur = *qp;
        uint8_t b[8];
        for (int i = 0; i < 8; i++) b[i] = (uint8_t)(cur >> (8 * i));
        if (!mem_eq(b + off, from, n)) break;
        for (int i = 0; i < n; i++) b[off + i] = to[i];
        uint64_t nv = 0;
        for (int i = 0; i < 8; i++) nv |= (uint64_t)b[i] << (8 * i);
        ok = __sync_bool_compare_and_swap(qp, cur, nv);
    }
    DWORD tmp;
    VirtualProtect((void *)(uintptr_t)q, 8, old, &tmp);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)q, 8);
    return ok;
}

static int group_status(const struct group *g)
{
    if (!build_supported()) return ST_UNSUPPORTED;
    int st = group_code_state(g);
    if (st < 0) return ST_RACE;
    return st ? ST_ON : ST_OFF;
}

/* Turn an option on or off. All of its sites change, or none do. */
static int group_set(const struct group *g, int on)
{
    if (!build_supported()) {
        log_line("unsupported game build or unexpected code bytes: no changes made");
        return ST_UNSUPPORTED;
    }
    int want = on ? 1 : 0;
    int cur = group_code_state(g);
    if (cur == want) return want ? ST_ON : ST_OFF;
    if (cur < 0) { log_line("code bytes in an unexpected state: no changes made"); return ST_RACE; }
    int done = 0, err = 0;
    for (; done < g->nsites; done++) {
        const struct site *s = &g->sites[done];
        int r = swap_bytes(s->va, s->n, on ? s->orig : s->patch, on ? s->patch : s->orig);
        if (r != 1) { err = r < 0 ? ST_PROTECT_FAIL : ST_RACE; break; }
    }
    if (err) {
        /* roll back the sites already changed so the option is never half-applied */
        for (int i = done - 1; i >= 0; i--) {
            const struct site *s = &g->sites[i];
            swap_bytes(s->va, s->n, on ? s->patch : s->orig, on ? s->orig : s->patch);
        }
        log_line(err == ST_PROTECT_FAIL ? "could not make game code writable: change rolled back"
                                        : "game code changed unexpectedly while patching: change rolled back");
        return err;
    }
    if (group_code_state(g) != want) { log_line("verification after change failed"); return ST_RACE; }
    log_line(on ? g->on_msg : g->off_msg);
    return want ? ST_ON : ST_OFF;
}

/* ---- Lua entry points (lua_CFunction: int f(lua_State *L)) ----------------------------- */
__declspec(dllexport) int irs_enable(void *L) { (void)L; return group_set(&g_groups[G_FEAR], 1); }
__declspec(dllexport) int irs_disable(void *L) { (void)L; return group_set(&g_groups[G_FEAR], 0); }
__declspec(dllexport) int irs_status(void *L) { (void)L; return group_status(&g_groups[G_FEAR]); }
__declspec(dllexport) int irs_nausea_enable(void *L) { (void)L; return group_set(&g_groups[G_NAUSEA], 1); }
__declspec(dllexport) int irs_nausea_disable(void *L) { (void)L; return group_set(&g_groups[G_NAUSEA], 0); }
__declspec(dllexport) int irs_nausea_status(void *L) { (void)L; return group_status(&g_groups[G_NAUSEA]); }

BOOL __stdcall DllMain(HANDLE inst, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        log_line(build_supported()
                     ? "Ignore Ride Safety " IRS_VERSION ": loaded, supported game build 1.13.3.88540"
                     : "Ignore Ride Safety " IRS_VERSION ": loaded, UNSUPPORTED game build or unexpected code - options disabled, nothing changed");
    }
    return 1;
}

/* Experimental unfinished/untested-ride work (inactive unless enabled from the experimental option). */
#include "irs_experimental.c"
