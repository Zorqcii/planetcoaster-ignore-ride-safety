/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Zorqcii
 *
 * DIAGNOSTIC 0.2.0-diag.2: logging-only observation of the game's own guest-physics chain.
 * Built instead of irs_experimental.c (build variant IRS_PHYSDIAG): NO gameplay patch, NO
 * experimental/diag.1/prototype code; the safety and nausea options refuse to turn on.
 *
 * Patch points: every patch replaces exactly ONE instruction by ONE instruction of the same length
 * at the same address, written with a single atomic aligned 8- or 16-byte compare-exchange. A thread
 * can therefore never be "inside" a patched span: it is either before the instruction (and executes
 * the old or the new one, both complete and valid) or after it.
 *   ENTRY  sites: a 5-byte first instruction (mov [rsp+x],reg) is replaced by `jmp stub`; the
 *                 trampoline executes the original instruction and jumps to the next one.
 *   REDIRECT sites: a `jmp rel32` / `call rel32` to the observed routine is re-targeted to the stub;
 *                 the wrapper then jumps to the original target.
 *
 * Read policy (hook bodies): a hook body reads only
 *   (a) values in argument registers;
 *   (b) memory on the CURRENT thread's stack (checked at run time against the thread's stack bounds);
 *   (c) game memory that the observed routine itself reads on this call before any call, lock or
 *       other synchronisation (verified offline per read), so the read happens under the same
 *       conditions as the game's own unsynchronised read.
 * Nothing else is read natively. Confirmed state (displayed behaviour, group members, messages) is
 * taken by the scripts through the game's public script functions on the script thread.
 *
 * Log labels: ENTRY = a routine was entered (values as read at entry, outcomes are predictions);
 * STATE = a confirmed change, from the scripts; SANITY = purge (unloading) calls from the script
 * binding and the station purge only - the crash-time purge call is not observable here.
 */

__declspec(dllimport) void *__stdcall VirtualAlloc(void *, uintptr_t, DWORD, DWORD);
__declspec(dllimport) uint64_t __stdcall GetTickCount64(void);
__declspec(dllimport) DWORD __stdcall GetCurrentThreadId(void);

int _fltused = 0;

#define PD_BUILD 3                  /* irs_pd_build: 3 = 0.2.0-diag.2 */

static char *fmt_u64(char *p, uint64_t v)
{
    char t[24]; int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) *p++ = t[--n];
    return p;
}
static char *fmt_str(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *fmt_hex(char *p, uint64_t v)
{
    *p++ = '0'; *p++ = 'x';
    int k = 60;
    while (k > 0 && ((v >> k) & 15) == 0) k -= 4;
    for (; k >= 0; k -= 4) *p++ = "0123456789abcdef"[(v >> k) & 15];
    return p;
}

/* ---- patch points ----------------------------------------------------------------------- */
enum { PK_ENTRY = 1, PK_REDIRECT = 2 };
struct pdsite {
    uint64_t va; int n; uint8_t orig[8]; uint8_t patch[8];
    int kind; int width;            /* atomic write unit: 8 or 16 bytes, aligned, containing the instruction */
    uint64_t target;                /* REDIRECT: original destination of the jmp/call */
    const char *name;
};
enum { S_IMPACT, S_POST, S_REQUEST, S_LAUNCH, S_SOS, S_RECOVER, S_EXIT, S_PURGE_SCRIPT, S_PURGE_STATION, PD_NSITES };
static struct pdsite g_pd_sites[PD_NSITES] = {
    {0x1406f1fa4ULL, 5, {0xe9, 0x07, 0x39, 0xfc, 0xff}, {0}, PK_REDIRECT, 16, 0x1406b58b0ULL, "impact receiver (dispatcher jmp)"},
    {0x1406f0a00ULL, 5, {0x48, 0x89, 0x5c, 0x24, 0x10}, {0}, PK_ENTRY, 8, 0, "post group request (entry)"},
    {0x1406f2634ULL, 5, {0xe9, 0x87, 0x65, 0xfb, 0xff}, {0}, PK_REDIRECT, 16, 0x1406a8bc0ULL, "request receiver (dispatcher jmp)"},
    {0x14067d450ULL, 5, {0x4c, 0x89, 0x4c, 0x24, 0x20}, {0}, PK_ENTRY, 8, 0, "physics start (entry)"},
    {0x140680af0ULL, 5, {0x48, 0x89, 0x5c, 0x24, 0x20}, {0}, PK_ENTRY, 8, 0, "SOS step (entry)"},
    {0x1406f2594ULL, 5, {0xe9, 0x37, 0x69, 0xfb, 0xff}, {0}, PK_REDIRECT, 16, 0x1406a8ed0ULL, "recovery receiver (dispatcher jmp)"},
    {0x14069cec0ULL, 5, {0x48, 0x89, 0x5c, 0x24, 0x20}, {0}, PK_ENTRY, 8, 0, "exit behaviour (entry)"},
    {0x14046c099ULL, 5, {0xe8, 0x02, 0xe0, 0x3a, 0x00}, {0}, PK_REDIRECT, 8, 0x14081a0a0ULL, "purge from script binding (call)"},
    {0x140856a86ULL, 5, {0xe8, 0x15, 0x36, 0xfc, 0xff}, {0}, PK_REDIRECT, 16, 0x14081a0a0ULL, "purge from station purge (call)"},
};

static const struct fp g_pd_fps[] = {
    /* P1 dispatcher stub: mov rcx,[rcx+8] (then jmp impact receiver = site) */
    {0x1406f1fa0ULL, 4, {0x48, 0x8b, 0x49, 0x08}},
    /* impact receiver: entry .. message 0 checks and pending-map lookup (reads mirrored by P1, all before its first call) */
    {0x1406b58b0ULL, 24, {0x4c, 0x8b, 0xdc, 0x53, 0x41, 0x54, 0x41, 0x57, 0x48, 0x83, 0xec, 0x60, 0x48, 0x8b, 0x5a, 0x10, 0x4c, 0x8b, 0xf9, 0x48, 0x8b, 0x42, 0x08, 0x45}},
    {0x1406b58c8ULL, 24, {0x33, 0xe4, 0x48, 0x8d, 0x04, 0xc3, 0x48, 0x8b, 0xc8, 0x48, 0x2b, 0xcb, 0x48, 0x83, 0xc1, 0x07, 0x48, 0xc1, 0xe9, 0x03, 0x48, 0x3b, 0xd8, 0x49}},
    {0x1406b58e0ULL, 24, {0x0f, 0x47, 0xcc, 0x48, 0x89, 0x4c, 0x24, 0x30, 0x48, 0x85, 0xc9, 0x0f, 0x84, 0x3e, 0x02, 0x00, 0x00, 0x49, 0x89, 0x73, 0xe0, 0x41, 0xb9, 0x42}},
    {0x1406b58f8ULL, 24, {0x11, 0x00, 0x00, 0x49, 0x89, 0x6b, 0x08, 0x49, 0x89, 0x7b, 0xd8, 0x4d, 0x89, 0x6b, 0xd0, 0x4d, 0x89, 0x73, 0xc8, 0x0f, 0x1f, 0x44, 0x00, 0x00}},
    {0x1406b5910ULL, 24, {0x48, 0x8b, 0x33, 0x48, 0x8b, 0x4e, 0x18, 0x48, 0x8b, 0xd1, 0x48, 0x8b, 0xc1, 0x48, 0xf7, 0xd0, 0x48, 0xc1, 0xe2, 0x12, 0x48, 0x03, 0xd0, 0x48}},
    {0x1406b5928ULL, 24, {0x8b, 0xc2, 0x48, 0xc1, 0xe8, 0x1f, 0x48, 0x33, 0xc2, 0x48, 0x6b, 0xd0, 0x15, 0x48, 0x8b, 0xc2, 0x48, 0xc1, 0xe8, 0x0b, 0x48, 0x33, 0xc2, 0x48}},
    {0x1406b5940ULL, 24, {0x6b, 0xd0, 0x41, 0x48, 0x8b, 0xc2, 0x8b, 0xd2, 0x48, 0xc1, 0xe8, 0x16, 0x8b, 0xc0, 0x48, 0x33, 0xc2, 0x33, 0xd2, 0x49, 0xf7, 0xb7, 0x20, 0x02}},
    {0x1406b5958ULL, 24, {0x00, 0x00, 0x49, 0x8b, 0x87, 0x18, 0x02, 0x00, 0x00, 0x4c, 0x8d, 0x04, 0xd0, 0x48, 0x8b, 0x04, 0xd0, 0x49, 0x3b, 0xc0, 0x0f, 0x84, 0x89, 0x01}},
    {0x1406b5970ULL, 24, {0x00, 0x00, 0x48, 0x3b, 0x48, 0x08, 0x74, 0x0d, 0x48, 0x8b, 0x00, 0x49, 0x3b, 0xc0, 0x75, 0xf2, 0xe9, 0x76, 0x01, 0x00, 0x00, 0x48, 0x85, 0xc0}},
    {0x1406b5988ULL, 24, {0x0f, 0x84, 0x6d, 0x01, 0x00, 0x00, 0x44, 0x8b, 0x50, 0x10, 0x49, 0x8b, 0x87, 0x88, 0x02, 0x00, 0x00, 0x41, 0x8b, 0xd2, 0x44, 0x89, 0x94, 0x24}},
    {0x1406b59a0ULL, 24, {0x88, 0x00, 0x00, 0x00, 0x4a, 0x39, 0x0c, 0xd0, 0x0f, 0x85, 0x4d, 0x01, 0x00, 0x00, 0x49, 0x8b, 0xbf, 0xd0, 0x01, 0x00, 0x00, 0x4c, 0x8d, 0x6e}},
    {0x1406b59b8ULL, 24, {0x38, 0x4c, 0x8b, 0x5e, 0x40, 0x48, 0x69, 0xca, 0x50, 0x02, 0x00, 0x00, 0x48, 0x03, 0x8f, 0xb8, 0x03, 0x00, 0x00, 0x80, 0x79, 0x08, 0x00, 0x0f}},
    {0x1406b59d0ULL, 24, {0x84, 0x26, 0x01, 0x00, 0x00, 0x0f, 0xb6, 0x41, 0x1a, 0x3c, 0x0c, 0x0f, 0x87, 0x1a, 0x01, 0x00, 0x00, 0x0f, 0xb6, 0xc0, 0x41, 0x0f, 0xa3, 0xc1}},
    {0x1406b59e8ULL, 24, {0x0f, 0x83, 0x0d, 0x01, 0x00, 0x00, 0x80, 0x79, 0x09, 0x00, 0x0f, 0x84, 0x03, 0x01, 0x00, 0x00, 0x41, 0x83, 0x7d, 0x00, 0x02, 0x75, 0x2d, 0x80}},
    {0x1406b5a00ULL, 24, {0xb9, 0xd8, 0x00, 0x00, 0x00, 0x01, 0x75, 0x24, 0x48, 0x8b, 0x8f, 0x58, 0x02, 0x00, 0x00, 0x48, 0x8d, 0x94, 0x24, 0x90, 0x00, 0x00, 0x00, 0x44}},
    {0x1406b5a18ULL, 24, {0x89, 0x94, 0x24, 0x90, 0x00, 0x00, 0x00, 0xe8, 0x6c, 0x85, 0x01, 0x00, 0x84, 0xc0, 0x0f, 0x85, 0xcf, 0x00, 0x00, 0x00, 0x4c, 0x3b, 0x9f, 0x50}},
    {0x1406b5a30ULL, 24, {0x9a, 0x00, 0x00, 0x0f, 0x84, 0xc2, 0x00, 0x00, 0x00, 0x49, 0x83, 0xbf, 0x50, 0x02, 0x00, 0x00, 0x00, 0x74, 0x63, 0x41, 0x69, 0xc2, 0x01, 0x10}},
    {0x1406b5a48ULL, 24, {0x00, 0x00, 0x33, 0xd2, 0x8b, 0xc8, 0xc1, 0xe9, 0x16, 0x33, 0xc8, 0x6b, 0xc1, 0x11, 0x8b, 0xc8, 0xc1, 0xe9, 0x09, 0x33, 0xc8, 0x69, 0xc1, 0x01}},
    {0x1406b5a60ULL, 24, {0x04, 0x00, 0x00, 0x8b, 0xc8, 0xc1, 0xe9, 0x02, 0x33, 0xc8, 0x69, 0xc1, 0x81, 0x00, 0x00, 0x00, 0x8b, 0xc8, 0x48, 0xc1, 0xe8, 0x0c, 0x48, 0x33}},
    {0x1406b5a78ULL, 24, {0xc1, 0x49, 0xf7, 0xb7, 0x40, 0x02, 0x00, 0x00, 0x49, 0x8b, 0x87, 0x38, 0x02, 0x00, 0x00, 0x48, 0x8d, 0x0c, 0xd0, 0x48, 0x8b, 0x04, 0xd0, 0x48}},
    {0x1406b5a90ULL, 22, {0x3b, 0xc1, 0x74, 0x12, 0x44, 0x3b, 0x50, 0x08, 0x0f, 0x84, 0x9b, 0x00, 0x00, 0x00, 0x48, 0x8b, 0x00, 0x48, 0x3b, 0xc1, 0x75, 0xee}},
    /* P2 post group request after entry */
    {0x1406f0a05ULL, 24, {0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x20, 0x8b, 0x05, 0x2f, 0x4e, 0x4e, 0x01, 0x48, 0x8b, 0xf2, 0x48, 0x8d, 0x54, 0x24, 0x30}},
    {0x1406f0a1dULL, 18, {0x89, 0x44, 0x24, 0x30, 0x49, 0x8b, 0xd9, 0x49, 0x8b, 0xf8, 0xe8, 0x04, 0xd1, 0x9d, 0xff, 0x48, 0x8b, 0x08}},
    /* P3 dispatcher stub: mov rcx,[rcx+8] (then jmp request receiver = site) */
    {0x1406f2630ULL, 4, {0x48, 0x8b, 0x49, 0x08}},
    /* request receiver: entry .. message 0 checks (reads mirrored by P3, all before its first call) */
    {0x1406a8bc0ULL, 24, {0x40, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xec, 0x38, 0x48, 0x8b, 0x5a, 0x10, 0x33, 0xff, 0x48, 0x8b, 0x42, 0x08, 0x48, 0x8b, 0xf1, 0x48}},
    {0x1406a8bd8ULL, 24, {0x8d, 0x04, 0xc3, 0x4c, 0x8b, 0xf0, 0x4c, 0x2b, 0xf3, 0x49, 0x83, 0xc6, 0x07, 0x49, 0xc1, 0xee, 0x03, 0x48, 0x3b, 0xd8, 0x4c, 0x0f, 0x47, 0xf7}},
    {0x1406a8bf0ULL, 24, {0x4d, 0x85, 0xf6, 0x0f, 0x84, 0xc8, 0x00, 0x00, 0x00, 0x48, 0x89, 0x6c, 0x24, 0x60, 0x4c, 0x89, 0x64, 0x24, 0x70, 0x41, 0xbc, 0x42, 0x11, 0x00}},
    {0x1406a8c08ULL, 24, {0x00, 0x4c, 0x89, 0x7c, 0x24, 0x30, 0x66, 0x90, 0x48, 0x8b, 0x2b, 0x48, 0x8b, 0x86, 0xb8, 0x03, 0x00, 0x00, 0x44, 0x8b, 0x55, 0x18, 0x4c, 0x8b}},
    {0x1406a8c20ULL, 24, {0x5d, 0x38, 0x4d, 0x69, 0xfa, 0x50, 0x02, 0x00, 0x00, 0x41, 0x80, 0x7c, 0x07, 0x08, 0x00, 0x49, 0x8d, 0x0c, 0x07, 0x74, 0x6d, 0x0f, 0xb6, 0x41}},
    {0x1406a8c38ULL, 24, {0x1a, 0x3c, 0x0c, 0x77, 0x65, 0x41, 0x0f, 0xa3, 0xc4, 0x73, 0x5f, 0x80, 0x79, 0x09, 0x00, 0x74, 0x59, 0x83, 0x7d, 0x1c, 0x02, 0x75, 0x23, 0x80}},
    {0x1406a8c50ULL, 24, {0xb9, 0xd8, 0x00, 0x00, 0x00, 0x01, 0x75, 0x1a, 0x48, 0x8b, 0x8e, 0x58, 0x02, 0x00, 0x00, 0x48, 0x8d, 0x54, 0x24, 0x68, 0x44, 0x89, 0x54, 0x24}},
    {0x1406a8c68ULL, 19, {0x68, 0xe8, 0x22, 0x53, 0x02, 0x00, 0x84, 0xc0, 0x75, 0x30, 0x4c, 0x3b, 0x9e, 0x50, 0x9a, 0x00, 0x00, 0x74, 0x27}},
    /* P4 physics start after entry .. call group lookup */
    {0x14067d455ULL, 24, {0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x48, 0x4c, 0x8b, 0xe2, 0x48, 0x8b, 0xe9, 0x48, 0x81}},
    {0x14067d46dULL, 16, {0xc1, 0x48, 0x02, 0x00, 0x00, 0x49, 0x8b, 0xd0, 0x49, 0x8b, 0xd8, 0xe8, 0x93, 0xbc, 0x06, 0x00}},
    /* group lookup callee prologue .. mov r8d,[rdx] (first memory read of the key) */
    {0x1406e9110ULL, 19, {0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7c, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xec, 0x30, 0x44, 0x8b, 0x02}},
    /* P5 SOS step after entry */
    {0x140680af5ULL, 23, {0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0x6c, 0x24, 0xf9, 0x48, 0x81, 0xec, 0xd0, 0x00, 0x00, 0x00}},
    /* P5 caller: r9 = &[rbp+0x160] (guest id on the caller's stack) */
    {0x14067fc89ULL, 24, {0x4c, 0x8d, 0x8d, 0x60, 0x01, 0x00, 0x00, 0x48, 0x8d, 0x45, 0xc4, 0x89, 0x75, 0xc8, 0x48, 0x89, 0x44, 0x24, 0x28, 0x4d, 0x8b, 0xc4, 0x48, 0x8d}},
    {0x14067fca1ULL, 24, {0x45, 0xc8, 0x48, 0x89, 0x9d, 0x60, 0x01, 0x00, 0x00, 0x49, 0x8b, 0xd7, 0x48, 0x89, 0x44, 0x24, 0x20, 0x49, 0x8b, 0xce, 0xe8, 0x36, 0x0e, 0x00}},
    {0x14067fcb9ULL, 1, {0x00}},
    /* P6 dispatcher stub: mov rcx,[rcx+8] (then jmp recovery receiver = site) */
    {0x1406f2590ULL, 4, {0x48, 0x8b, 0x49, 0x08}},
    /* recovery receiver: entry .. call exit-behaviour (reads mirrored by P6) */
    {0x1406a8ed0ULL, 24, {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x5a, 0x10}},
    {0x1406a8ee8ULL, 24, {0x33, 0xff, 0x48, 0x8b, 0x42, 0x08, 0x48, 0x8b, 0xe9, 0x48, 0x8d, 0x04, 0xc3, 0x48, 0x8b, 0xf0, 0x48, 0x2b, 0xf3, 0x48, 0x83, 0xc6, 0x07, 0x48}},
    {0x1406a8f00ULL, 24, {0xc1, 0xee, 0x03, 0x48, 0x3b, 0xd8, 0x48, 0x0f, 0x47, 0xf7, 0x48, 0x85, 0xf6, 0x74, 0x4a, 0x90, 0x48, 0x8b, 0x03, 0x8b, 0x40, 0x18, 0x4c, 0x69}},
    {0x1406a8f18ULL, 24, {0xc0, 0x50, 0x02, 0x00, 0x00, 0x4c, 0x03, 0x85, 0xb8, 0x03, 0x00, 0x00, 0x41, 0x80, 0x78, 0x1a, 0x0b, 0x75, 0x22, 0x41, 0x80, 0x78, 0x08, 0x00}},
    {0x1406a8f30ULL, 24, {0x74, 0x1b, 0x41, 0x80, 0x78, 0x09, 0x00, 0x74, 0x14, 0x45, 0x33, 0xc9, 0x89, 0x44, 0x24, 0x38, 0x48, 0x8d, 0x54, 0x24, 0x38, 0x48, 0x8b, 0xcd}},
    {0x1406a8f48ULL, 5, {0xe8, 0x73, 0x3f, 0xff, 0xff}},
    /* P7 exit behaviour after entry .. [r8+0x1b], [r8+0x50] read before its first call */
    {0x14069cec5ULL, 24, {0x55, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x48, 0x8b, 0xec, 0x48, 0x83, 0xec, 0x60, 0x41, 0x80, 0x78, 0x1b, 0x00, 0x45, 0x0f, 0xb6, 0xf1, 0x49}},
    {0x14069ceddULL, 24, {0x8b, 0xf0, 0x4c, 0x8b, 0xea, 0x48, 0x8b, 0xf9, 0x74, 0x35, 0x49, 0x83, 0x78, 0x50, 0xff, 0x74, 0x2a, 0x48, 0x8b, 0x89, 0x98, 0x01, 0x00, 0x00}},
    {0x14069cef5ULL, 8, {0x49, 0x8d, 0x50, 0x50, 0x48, 0x83, 0xc1, 0x08}},
    /* enter-Physics: unconditional call to exit behaviour (return address 0x14069c8d1) */
    {0x14069c8b3ULL, 24, {0x8b, 0x02, 0x49, 0x8b, 0xd9, 0x4c, 0x8b, 0xea, 0x41, 0x89, 0x43, 0x10, 0x45, 0x33, 0xc9, 0x49, 0x8d, 0x53, 0x10, 0x4d, 0x8b, 0xf0, 0x4c, 0x8b}},
    {0x14069c8cbULL, 6, {0xf9, 0xe8, 0xef, 0x05, 0x00, 0x00}},
    /* P8 script binding: builds the id vector on its stack, then call purge = site */
    {0x14046c08cULL, 13, {0xe8, 0x2f, 0x55, 0xcf, 0xff, 0x48, 0x8d, 0x54, 0x24, 0x20, 0x48, 0x8b, 0xcb}},
    /* P9 station purge: builds the id vector, then call purge = site */
    {0x140856a79ULL, 13, {0xe8, 0x42, 0xab, 0x90, 0xff, 0x48, 0x8d, 0x54, 0x24, 0x20, 0x48, 0x8b, 0xcb}},
    /* purge: entry .. reads [rdx+0x18], [rdx+0x10], first id (before its first call) */
    {0x14081a0a0ULL, 24, {0x48, 0x8b, 0xc4, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0xa8, 0x18, 0xfd, 0xff, 0xff, 0x48, 0x81}},
    {0x14081a0b8ULL, 24, {0xec, 0xa8, 0x03, 0x00, 0x00, 0x48, 0x8b, 0x72, 0x18, 0x45, 0x33, 0xff, 0xf3, 0x0f, 0x10, 0x05, 0x38, 0x51, 0x22, 0x01, 0x4c, 0x8b, 0xe9, 0x0f}},
    {0x14081a0d0ULL, 24, {0x29, 0x70, 0xa8, 0x45, 0x8b, 0xcf, 0x0f, 0x29, 0x78, 0x98, 0x45, 0x8b, 0xd7, 0x44, 0x0f, 0x29, 0x40, 0x88, 0x44, 0x0f, 0x29, 0x88, 0x78, 0xff}},
    {0x14081a0e8ULL, 24, {0xff, 0xff, 0x44, 0x0f, 0x29, 0x90, 0x68, 0xff, 0xff, 0xff, 0x44, 0x0f, 0x29, 0x98, 0x58, 0xff, 0xff, 0xff, 0x48, 0x8b, 0x42, 0x10, 0xf3, 0x0f}},
    {0x14081a100ULL, 24, {0x11, 0x44, 0x24, 0x70, 0x4c, 0x89, 0x7c, 0x24, 0x60, 0x4c, 0x89, 0x7c, 0x24, 0x58, 0x4c, 0x8d, 0x34, 0xc6, 0x4c, 0x89, 0x7c, 0x24, 0x68, 0x48}},
    {0x14081a118ULL, 24, {0x89, 0x74, 0x24, 0x40, 0x4c, 0x89, 0x75, 0x80, 0x49, 0x3b, 0xf6, 0x0f, 0x84, 0x5c, 0x06, 0x00, 0x00, 0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00}},
    {0x14081a130ULL, 4, {0x48, 0x8b, 0x0e, 0x48}},
};

/* ---- event ring (hooks write, script thread reads) --------------------------------------- */
enum { K_IMPACT = 1, K_POST, K_REQUEST, K_LAUNCH, K_SOS, K_RECOVER, K_EXIT, K_PURGE, K_KINDS };
struct pd_ev { volatile uint64_t seq; uint32_t kind, tid; uint64_t t, ret, a, b, c, d, e; };
#define PD_RING 1024
static struct pd_ev g_pd_ring[PD_RING];
static volatile uint64_t g_pd_head;
static uint64_t g_pd_tail;
static uint64_t g_pd_t0;
static long g_pd_printed;
#define PD_PRINT_BUDGET 5000
#define PD_PRINT_PER_REPORT 64
static volatile int g_pd_on;

static void pd_event(uint32_t kind, uint64_t ret, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e)
{
    if (!g_pd_on || kind >= K_KINDS) return;
    uint64_t i = __sync_fetch_and_add(&g_pd_head, 1);
    struct pd_ev *v = &g_pd_ring[i & (PD_RING - 1)];
    v->seq = 0;
    __sync_synchronize();
    v->kind = kind; v->tid = GetCurrentThreadId(); v->t = GetTickCount64(); v->ret = ret;
    v->a = a; v->b = b; v->c = c; v->d = d; v->e = e;
    __sync_synchronize();
    v->seq = i + 1;
}

/* ids for the scripts (kind 1 = group index, 2 = guest id) */
#define PD_ITEMS 128
static volatile uint64_t g_pd_items[PD_ITEMS];
static volatile uint64_t g_pd_items_head;
static uint64_t g_pd_items_tail;
static void pd_item(uint64_t kind, uint64_t value)
{
    if (!g_pd_on || value >> 61) return;
    uint64_t i = __sync_fetch_and_add(&g_pd_items_head, 1);
    g_pd_items[i & (PD_ITEMS - 1)] = (value << 2) | kind;
}

/* groups of interest (filled from observed request/recovery/launch ids; read by the exit hook) */
#define PD_GROUPS 32
static volatile uint64_t g_pd_groups[PD_GROUPS];   /* idx + 1, 0 = empty */
static void pd_group_add(uint64_t idx)
{
    for (int i = 0; i < PD_GROUPS; i++) if (g_pd_groups[i] == idx + 1) return;
    for (int i = 0; i < PD_GROUPS; i++)
        if (__sync_bool_compare_and_swap(&g_pd_groups[i], 0, idx + 1)) { pd_item(1, idx); return; }
}
static int pd_group_known(uint64_t idx)
{
    for (int i = 0; i < PD_GROUPS; i++) if (g_pd_groups[i] == idx + 1) return 1;
    return 0;
}

/* ---- read helpers -------------------------------------------------------------------------- */
#define RD8(p)  (*(const uint8_t *)(uintptr_t)(p))
#define RD32(p) (*(const uint32_t *)(uintptr_t)(p))
#define RD64(p) (*(const uint64_t *)(uintptr_t)(p))

/* (b) current thread's stack: NT_TIB StackBase gs:[8], StackLimit gs:[0x10] */
static int pd_on_my_stack(uint64_t p, uint64_t size)
{
    uint64_t base, limit;
    __asm__ volatile("mov %%gs:0x8, %0" : "=r"(base));
    __asm__ volatile("mov %%gs:0x10, %0" : "=r"(limit));
    return p >= limit && p + size <= base && p + size > p;
}

/* game hash lookups, read-only (fingerprinted; checked against the game's code in an emulator) */
static const uint64_t *pd_find64(const uint64_t *m, uint64_t key)
{
    const uint64_t *buckets = (const uint64_t *)(uintptr_t)m[0];
    uint64_t nb = m[1];
    if (!buckets || !nb) return 0;
    uint64_t h = (key << 18) + ~key;
    h ^= h >> 31; h *= 21; h ^= h >> 11; h *= 65;
    const uint64_t *slot = &buckets[((uint64_t)(uint32_t)(h >> 22) ^ (uint64_t)(uint32_t)h) % nb];
    const uint64_t *node = (const uint64_t *)(uintptr_t)*slot;
    for (int g = 0; node && node != slot && g < 100000; g++) {
        if (node[1] == key) return node;
        node = (const uint64_t *)(uintptr_t)node[0];
    }
    return 0;
}
static const uint64_t *pd_find32(const uint64_t *m, uint32_t key, int keyoff)
{
    const uint64_t *buckets = (const uint64_t *)(uintptr_t)m[0];
    uint64_t nb = m[1];
    if (!buckets || !nb) return 0;
    uint32_t a = key * 0x1001u;
    a ^= a >> 22; a *= 0x11u; a ^= a >> 9; a *= 0x401u; a ^= a >> 2; a *= 0x81u;
    const uint64_t *slot = &buckets[((uint64_t)(a >> 12) ^ (uint64_t)a) % nb];
    const uint64_t *node = (const uint64_t *)(uintptr_t)*slot;
    for (int g = 0; node && node != slot && g < 100000; g++) {
        if (*(const uint32_t *)((const uint8_t *)node + keyoff) == key) return node;
        node = (const uint64_t *)(uintptr_t)node[0];
    }
    return 0;
}

static int pd_beh_ok(uint8_t beh) { return beh <= 0xc && ((0x1142u >> beh) & 1); }
enum { PRED_REJECT = 0, PRED_ACCEPT = 1, PRED_EXTRA = 2, PRED_NA = 3 };
#define NOTREAD 0xffffffffffffffffULL

/* ---- hook bodies: regs [0]=r9 [1]=r8 [2]=rdx [3]=rcx [4]=return address [4+k]=[entry rsp + 8k] ----
 * Each body mirrors, in the same order, only the reads the observed routine makes on this call
 * before its first call or synchronisation (see the offline read-path check). Message lists: only
 * message 0 is examined (later messages are read by the game after calls). */

/* P1 impact receiver 0x1406b58b0 (rcx = system, rdx = message list) */
void pd_b_impact(const uint64_t *r)
{
    uint64_t sys = r[3], vec = r[2];
    uint64_t data = RD64(vec + 0x10), cnt = RD64(vec + 8);
    if (!cnt) return;
    uint64_t msg = RD64(data);
    uint64_t guest = RD64(msg + 0x18);
    uint64_t idx = NOTREAD, packed = (uint64_t)PRED_NA << 36, reason = NOTREAD, src = NOTREAD;
    const uint64_t *node = pd_find64((const uint64_t *)(uintptr_t)(sys + 0x218), guest);
    if (node) {
        idx = RD32((uint64_t)(uintptr_t)node + 0x10);
        packed = 1ULL << 38;                                   /* found */
        if (RD64(RD64(sys + 0x288) + idx * 8) == guest) {
            packed |= 1ULL << 39;                              /* per-group entry matches */
            uint64_t gm = RD64(sys + 0x1d0);
            uint64_t rec = RD64(gm + 0x3b8) + idx * 0x250;
            uint8_t f8 = RD8(rec + 8), beh = 0, f9 = 0, d8 = 0;
            int pred = PRED_REJECT, srcEq = 0, pend = 0;
            if (f8) {
                beh = RD8(rec + 0x1a);
                if (pd_beh_ok(beh)) {
                    f9 = RD8(rec + 9);
                    if (f9) {
                        reason = RD32(msg + 0x38);
                        if (reason == 2) d8 = RD8(rec + 0xd8);
                        if (reason == 2 && d8 == 1) {
                            pred = PRED_EXTRA;                 /* the game calls 0x1406cdf90 here: stop reading */
                        } else {
                            src = RD64(msg + 0x40);
                            srcEq = src == RD64(gm + 0x9a50);
                            if (!srcEq) {
                                if (RD64(sys + 0x250)) pend = pd_find32((const uint64_t *)(uintptr_t)(sys + 0x238), (uint32_t)idx, 8) ? 1 : 0;
                                else pend = 2;
                                pred = pend == 1 ? PRED_REJECT : PRED_ACCEPT;
                            }
                        }
                    }
                }
            }
            packed |= (uint64_t)f8 | ((uint64_t)f9 << 8) | ((uint64_t)beh << 16) | ((uint64_t)d8 << 24) |
                      ((uint64_t)srcEq << 32) | ((uint64_t)pend << 33) | ((uint64_t)pred << 36);
        }
    }
    pd_event(K_IMPACT, r[4], guest, idx, reason, src, packed | (cnt > 1 ? 1ULL << 40 : 0));
    pd_item(2, guest);
}

/* P2 post group request 0x1406f0a00 (rdx = &group index on the caller's stack) */
void pd_b_post(const uint64_t *r)
{
    uint64_t idx = pd_on_my_stack(r[2], 4) ? RD32(r[2]) : NOTREAD;
    pd_event(K_POST, r[4], idx, 0, 0, 0, 0);
    if (idx != NOTREAD) pd_group_add(idx);
}

/* P3 request receiver 0x1406a8bc0 (rcx = guest manager, rdx = message list) */
void pd_b_request(const uint64_t *r)
{
    uint64_t gm = r[3], vec = r[2];
    uint64_t data = RD64(vec + 0x10), cnt = RD64(vec + 8);
    if (!cnt) return;
    uint64_t msg = RD64(data);
    uint64_t idx = RD32(msg + 0x18), src = RD64(msg + 0x38);
    uint64_t rec = RD64(gm + 0x3b8) + idx * 0x250;
    uint8_t f8 = RD8(rec + 8), beh = 0, f9 = 0, d8 = 0;
    uint64_t reason = NOTREAD;
    int pred = PRED_REJECT, srcEq = 0;
    if (f8) {
        beh = RD8(rec + 0x1a);
        if (pd_beh_ok(beh)) {
            f9 = RD8(rec + 9);
            if (f9) {
                reason = RD32(msg + 0x1c);
                if (reason == 2) d8 = RD8(rec + 0xd8);
                if (reason == 2 && d8 == 1) pred = PRED_EXTRA;
                else { srcEq = src == RD64(gm + 0x9a50); if (!srcEq) pred = PRED_ACCEPT; }
            }
        }
    }
    uint64_t packed = (uint64_t)f8 | ((uint64_t)f9 << 8) | ((uint64_t)beh << 16) | ((uint64_t)d8 << 24) |
                      ((uint64_t)srcEq << 32) | ((uint64_t)pred << 36) | (cnt > 1 ? 1ULL << 40 : 0);
    pd_event(K_REQUEST, r[4], idx, reason, src, 0, packed);
    pd_group_add(idx);
}

/* P4 physics start 0x14067d450 (rdx = &guest id, r8 = &{group key, ...}) */
void pd_b_launch(const uint64_t *r)
{
    uint64_t key = RD32(r[1]);                                /* first memory read of the routine's first callee */
    uint64_t guest = pd_on_my_stack(r[2], 8) ? RD64(r[2]) : NOTREAD;
    pd_event(K_LAUNCH, r[4], guest, key, 0, 0, 0);
    pd_group_add(key);
    if (guest != NOTREAD) pd_item(2, guest);
}

/* P5 SOS step 0x140680af0 (r9 = &guest id on the caller's stack) */
void pd_b_sos(const uint64_t *r)
{
    uint64_t guest = pd_on_my_stack(r[0], 8) ? RD64(r[0]) : NOTREAD;
    pd_event(K_SOS, r[4], guest, 0, 0, 0, 0);
    if (guest != NOTREAD) pd_item(2, guest);
}

/* P6 recovery receiver 0x1406a8ed0 (rcx = guest manager, rdx = message list) */
void pd_b_recover(const uint64_t *r)
{
    uint64_t gm = r[3], vec = r[2];
    uint64_t data = RD64(vec + 0x10), cnt = RD64(vec + 8);
    if (!cnt) return;
    uint64_t msg = RD64(data);
    uint64_t idx = RD32(msg + 0x18);
    uint64_t rec = RD64(gm + 0x3b8) + idx * 0x250;
    uint8_t beh = RD8(rec + 0x1a), f8 = 0, f9 = 0;
    int proceed = 0;
    if (beh == 0x0b) { f8 = RD8(rec + 8); if (f8) { f9 = RD8(rec + 9); proceed = f9 != 0; } }
    pd_event(K_RECOVER, r[4], idx, beh, f8, f9, (uint64_t)proceed | (cnt > 1 ? 2 : 0));
    pd_group_add(idx);
}

/* P7 exit behaviour 0x14069cec0 (rdx = &group index, r8 = group record); logged only when called by
 * enter-Physics or by the recovery receiver, or for a group already of interest */
#define RET_FROM_ENTER   0x14069c8d1ULL
#define RET_FROM_RECOVER 0x1406a8f4dULL
void pd_b_exit(const uint64_t *r)
{
    uint64_t idx = pd_on_my_stack(r[2], 4) ? RD32(r[2]) : NOTREAD;
    int interesting = r[4] == RET_FROM_ENTER || r[4] == RET_FROM_RECOVER || (idx != NOTREAD && pd_group_known(idx));
    if (!interesting) return;
    uint64_t rec = r[1];
    uint8_t f1b = RD8(rec + 0x1b);                           /* read by the routine first */
    uint64_t handle = f1b ? RD64(rec + 0x50) : NOTREAD;      /* read next, only when +0x1b != 0 */
    pd_event(K_EXIT, r[4], idx, f1b, handle, r[0] & 0xff, 0);
    if (idx != NOTREAD) pd_group_add(idx);
}

/* P8/P9 purge 0x14081a0a0 via its callers (rdx = id vector) */
void pd_b_purge(const uint64_t *r)
{
    uint64_t v = r[2];
    uint64_t data = RD64(v + 0x18), n = RD64(v + 0x10), first = 0;
    if (n) first = RD64(data);
    pd_event(K_PURGE, r[4], n, first, 0, 0, 0);
}

/* ---- wrappers ----------------------------------------------------------------------------
 * Entered by jmp (ENTRY: from the patched entry; REDIRECT: from the re-targeted jmp/call), so the
 * stack is exactly as at the observed routine's entry ([rsp] = return address, rsp = 8 mod 16).
 * Saves rcx, rdx, r8, r9 and xmm0-xmm3; the body may change only rax, r10, r11, xmm4, xmm5 and
 * the flags, which the observed routines do not read before writing (offline check). Ends with a
 * jump through g_pd_tramp[i] (ENTRY: trampoline; REDIRECT: original destination). */
#define PD_WRAPPER(name, body, tramp) \
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
void *g_pd_tramp[PD_NSITES];
#define PD_W(i, name, body) void name(void); PD_WRAPPER(name, body, g_pd_tramp + 8 * i)
PD_W(0, pd_w_impact, pd_b_impact);
PD_W(1, pd_w_post, pd_b_post);
PD_W(2, pd_w_request, pd_b_request);
PD_W(3, pd_w_launch, pd_b_launch);
PD_W(4, pd_w_sos, pd_b_sos);
PD_W(5, pd_w_recover, pd_b_recover);
PD_W(6, pd_w_exit, pd_b_exit);
PD_W(7, pd_w_purge_script, pd_b_purge);
PD_W(8, pd_w_purge_station, pd_b_purge);

/* ---- stub page: site i -> stub at 0x40*i (jmp [rip+0] -> wrapper); ENTRY trampoline at
 * 0x40*i+0x20 (original instruction, jmp [rip+0] -> site+n). Also computes the patch bytes and the
 * jump slots. Pure function of its inputs (tested offline). */
static void pd_put_abs_jmp(uint8_t *p, uint64_t target)
{
    p[0] = 0xff; p[1] = 0x25; p[2] = p[3] = p[4] = p[5] = 0;
    for (int i = 0; i < 8; i++) p[6 + i] = (uint8_t)(target >> (8 * i));
}
static int pd_build_page(uint8_t *page, uint64_t page_va, const uint64_t *wrappers, struct pdsite *sites, int nsites,
                         uint64_t *slots)
{
    for (int i = 0; i < nsites; i++) {
        struct pdsite *s = &sites[i];
        if (s->n != 5) return 0;
        uint8_t *stub = page + 0x40 * i;
        pd_put_abs_jmp(stub, wrappers[i]);
        int64_t rel = (int64_t)(page_va + 0x40 * (uint64_t)i) - (int64_t)(s->va + 5);
        if (rel > 0x7fffffffLL || rel < -0x80000000LL) return 0;
        if (s->kind == PK_ENTRY) {
            uint8_t *tr = stub + 0x20;
            for (int k = 0; k < 5; k++) tr[k] = s->orig[k];
            pd_put_abs_jmp(tr + 5, s->va + 5);
            slots[i] = page_va + 0x40 * (uint64_t)i + 0x20;
            s->patch[0] = 0xe9;                                    /* jmp stub */
        } else {
            slots[i] = s->target;
            s->patch[0] = s->orig[0];                              /* same opcode (e8 call / e9 jmp), new target */
        }
        for (int k = 0; k < 4; k++) s->patch[1 + k] = (uint8_t)((uint64_t)rel >> (8 * k));
    }
    return 1;
}

/* ---- atomic single-instruction swap within one aligned 8- or 16-byte unit -------------------- */
static int pd_swap(const struct pdsite *s, const uint8_t *from, const uint8_t *to)
{
    uint64_t unit = s->width == 16 ? 16 : 8;
    uint64_t q = s->va & ~(unit - 1);
    unsigned off = (unsigned)(s->va - q);
    if (off + (unsigned)s->n > unit) return 0;
    DWORD old;
    if (!VirtualProtect((void *)(uintptr_t)q, unit, PAGE_EXECUTE_READWRITE, &old)) return -1;
    int ok = 0;
    for (int tries = 0; tries < 8 && !ok; tries++) {
        if (unit == 8) {
            volatile uint64_t *qp = (volatile uint64_t *)(uintptr_t)q;
            uint64_t cur = *qp, nv = cur;
            uint8_t *bc = (uint8_t *)&cur, *bn = (uint8_t *)&nv;
            if (!mem_eq(bc + off, from, s->n)) break;
            for (int i = 0; i < s->n; i++) bn[off + i] = to[i];
            ok = __sync_bool_compare_and_swap(qp, cur, nv);
        } else {
            volatile unsigned __int128 *qp = (volatile unsigned __int128 *)(uintptr_t)q;
            unsigned __int128 cur = __sync_val_compare_and_swap(qp, (unsigned __int128)0, (unsigned __int128)0);  /* atomic read */
            unsigned __int128 nv = cur;
            uint8_t *bc = (uint8_t *)&cur, *bn = (uint8_t *)&nv;
            if (!mem_eq(bc + off, from, s->n)) break;
            for (int i = 0; i < s->n; i++) bn[off + i] = to[i];
            ok = __sync_bool_compare_and_swap(qp, cur, nv);
        }
    }
    DWORD tmp;
    VirtualProtect((void *)(uintptr_t)q, unit, old, &tmp);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)q, unit);
    return ok;
}

/* serialise this thread's instruction stream after modifying code */
static void pd_serialize(void)
{
    uint32_t a = 0, b, c = 0, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d) : : "memory");
    (void)b; (void)d;
}

/* ---- install / remove -------------------------------------------------------------------- */
static int g_pd_supported = -1;
static uint8_t *g_pd_page;

static int pd_supported(void)
{
    if (g_pd_supported != -1) return g_pd_supported;
    g_pd_supported = 0;
    if (!build_supported()) return 0;
    for (unsigned i = 0; i < sizeof g_pd_fps / sizeof g_pd_fps[0]; i++)
        if (!mem_eq((const uint8_t *)(uintptr_t)g_pd_fps[i].va, g_pd_fps[i].bytes, g_pd_fps[i].n)) return 0;
    for (int i = 0; i < PD_NSITES; i++) {
        const struct pdsite *s = &g_pd_sites[i];
        uint64_t unit = s->width == 16 ? 16 : 8;
        if ((s->va & (unit - 1)) + (uint64_t)s->n > unit) return 0;
        if (!mem_eq((const uint8_t *)(uintptr_t)s->va, s->orig, s->n)) return 0;
        if (s->kind == PK_REDIRECT) {                             /* original rel32 must reach the expected target */
            int32_t rel = (int32_t)RD32(s->va + 1);
            if (s->va + 5 + (int64_t)rel != s->target) return 0;
        }
    }
    g_pd_supported = 1;
    return 1;
}

static int pd_prepare(void)
{
    if (g_pd_page) return 1;
    uint8_t *page = 0;
    for (uint64_t a = 0x147800000ULL; a < 0x1c0000000ULL && !page; a += 0x10000)
        page = (uint8_t *)VirtualAlloc((void *)(uintptr_t)a, 0x1000, 0x3000, PAGE_EXECUTE_READWRITE);
    for (uint64_t a = 0x13f000000ULL; a > 0xc8000000ULL && !page; a -= 0x10000)
        page = (uint8_t *)VirtualAlloc((void *)(uintptr_t)a, 0x1000, 0x3000, PAGE_EXECUTE_READWRITE);
    if (!page) { log_line("diag: could not allocate the stub page near the game image"); return 0; }
    const uint64_t w[PD_NSITES] = {
        (uint64_t)(uintptr_t)&pd_w_impact, (uint64_t)(uintptr_t)&pd_w_post, (uint64_t)(uintptr_t)&pd_w_request,
        (uint64_t)(uintptr_t)&pd_w_launch, (uint64_t)(uintptr_t)&pd_w_sos, (uint64_t)(uintptr_t)&pd_w_recover,
        (uint64_t)(uintptr_t)&pd_w_exit, (uint64_t)(uintptr_t)&pd_w_purge_script, (uint64_t)(uintptr_t)&pd_w_purge_station};
    uint64_t slots[PD_NSITES];
    if (!pd_build_page(page, (uint64_t)(uintptr_t)page, w, g_pd_sites, PD_NSITES, slots)) {
        log_line("diag: stub page out of range");
        return 0;
    }
    for (int i = 0; i < PD_NSITES; i++) g_pd_tramp[i] = (void *)(uintptr_t)slots[i];
    FlushInstructionCache(GetCurrentProcess(), page, 0x40 * PD_NSITES);
    __sync_synchronize();
    g_pd_page = page;
    return 1;
}

static int pd_code_state(void)
{
    int off = 0, on = 0;
    for (int i = 0; i < PD_NSITES; i++) {
        const struct pdsite *s = &g_pd_sites[i];
        const uint8_t *p = (const uint8_t *)(uintptr_t)s->va;
        if (mem_eq(p, s->orig, s->n)) off++;
        else if (g_pd_page && mem_eq(p, s->patch, s->n)) on++;
    }
    return off == PD_NSITES ? 0 : on == PD_NSITES ? 1 : -1;
}

static int pd_set(int on)
{
    if (!pd_supported()) { if (on) log_line("diag: unexpected game code - hooks not installed"); return ST_UNSUPPORTED; }
    if (on && !pd_prepare()) return ST_PROTECT_FAIL;
    int cur = pd_code_state();
    if (cur == on) { g_pd_on = on; return on ? ST_ON : ST_OFF; }
    if (cur < 0) { log_line("diag: patch points in an unexpected state - not changed"); return ST_RACE; }
    if (on) { if (!g_pd_t0) g_pd_t0 = GetTickCount64(); g_pd_on = 1; }
    int done = 0, err = 0;
    for (; done < PD_NSITES; done++) {
        int i = on ? done : PD_NSITES - 1 - done;
        const struct pdsite *s = &g_pd_sites[i];
        int r = pd_swap(s, on ? s->orig : s->patch, on ? s->patch : s->orig);
        if (r != 1) { err = r < 0 ? ST_PROTECT_FAIL : ST_RACE; break; }
    }
    if (err) {
        for (int k = done - 1; k >= 0; k--) {
            int i = on ? k : PD_NSITES - 1 - k;
            const struct pdsite *s = &g_pd_sites[i];
            pd_swap(s, on ? s->patch : s->orig, on ? s->orig : s->patch);
        }
        pd_serialize();
        log_line("diag: patch change failed and was rolled back");
        if (on) g_pd_on = 0;
        return err;
    }
    pd_serialize();
    int st = pd_code_state();
    if (on) log_line(st == 1 ? "diag: 9 single-instruction observation patches installed and read back OK (no gameplay patches in this build)"
                             : "diag: READ-BACK FAILED after install");
    else {
        g_pd_on = 0;
        log_line(st == 0 ? "diag: patches removed, original instructions read back OK" : "diag: READ-BACK FAILED after removal");
    }
    return on ? ST_ON : ST_OFF;
}

/* ---- report (script thread) -------------------------------------------------------------- */
static const char *pd_beh_name(uint64_t b)
{
    static const char *n[] = {"Idle", "Navigating", "Lost", "Queueing", "OnRide", "Exiting", "AtShop", "AtBench", "AtEntertainer",
                              "Suspended", "Trapped", "Physics", "AtSecurityGuard", "AtVandalismTarget", "WatchingFireworks",
                              "WatchingScreen", "AtVistaPoint", "ExternallyHandled", "AtGhost"};
    return b < 19 ? n[b] : "?";
}
static const char *pd_caller(uint64_t ret)
{
    switch (ret) {
    case 0x1406b5ae1ULL: return "impact receiver";
    case 0x1406b50c1ULL: return "0x1406b4c30 (pending-request path)";
    case 0x14408ee69ULL: return "protected caller 0x14408ee20";
    case 0x14067fcbaULL: return "guest-physics update";
    case RET_FROM_ENTER: return "enter-Physics";
    case RET_FROM_RECOVER: return "recovery receiver";
    case 0x14046c09eULL: return "script rides:PurgeAllRideGuests";
    case 0x140856a8bULL: return "station purge";
    default: return 0;
    }
}
static char *pd_val(char *p, uint64_t v) { return v == NOTREAD ? fmt_str(p, "(not read)") : fmt_u64(p, v); }
static char *pd_checks(char *p, uint64_t e)
{
    static const char *pred[] = {"predicted REJECT", "predicted ACCEPT", "needs extra check 0x1406cdf90 (not evaluated; later checks not read)", "n/a"};
    p = fmt_str(p, " checks: +8="); p = fmt_u64(p, e & 0xff);
    p = fmt_str(p, " beh="); p = fmt_str(p, pd_beh_name((e >> 16) & 0xff));
    p = fmt_str(p, " +9="); p = fmt_u64(p, (e >> 8) & 0xff);
    p = fmt_str(p, " +0xd8="); p = fmt_u64(p, (e >> 24) & 0xff);
    p = fmt_str(p, " src==+0x9a50:"); p = fmt_u64(p, (e >> 32) & 1);
    p = fmt_str(p, " => "); p = fmt_str(p, pred[(e >> 36) & 3]);
    return p;
}

static void pd_line(const struct pd_ev *v, uint64_t idx)
{
    char b[420], *p = b;
    p = fmt_str(p, "pd #"); p = fmt_u64(p, idx + 1);
    p = fmt_str(p, " t="); p = fmt_u64(p, v->t - g_pd_t0); p = fmt_str(p, "ms tid="); p = fmt_u64(p, v->tid); *p++ = ' ';
    switch (v->kind) {
    case K_IMPACT:
        p = fmt_str(p, "ENTRY impact-event(msg 0) guest="); p = fmt_u64(p, v->a);
        if (v->b == NOTREAD) p = fmt_str(p, " grp=(not in guest->group map) => predicted REJECT");
        else {
            p = fmt_str(p, " grp="); p = fmt_u64(p, v->b);
            if (!((v->e >> 39) & 1)) p = fmt_str(p, " => predicted REJECT (per-group entry mismatch)");
            else {
                p = fmt_str(p, " reason="); p = pd_val(p, v->c); p = fmt_str(p, " src="); p = v->d == NOTREAD ? fmt_str(p, "(not read)") : fmt_hex(p, v->d);
                p = pd_checks(p, v->e);
                uint64_t pend = (v->e >> 33) & 3;
                p = fmt_str(p, pend == 1 ? " (request already pending)" : "");
            }
        }
        if ((v->e >> 40) & 1) p = fmt_str(p, " [more messages in this batch: not examined]");
        break;
    case K_POST:
        p = fmt_str(p, "ENTRY post-group-request grp="); p = pd_val(p, v->a);
        break;
    case K_REQUEST:
        p = fmt_str(p, "ENTRY request-receiver(msg 0) grp="); p = fmt_u64(p, v->a);
        p = fmt_str(p, " reason="); p = pd_val(p, v->b); p = fmt_str(p, " src="); p = fmt_hex(p, v->c);
        p = pd_checks(p, v->e);
        if ((v->e >> 40) & 1) p = fmt_str(p, " [more messages in this batch: not examined]");
        break;
    case K_LAUNCH:
        p = fmt_str(p, "ENTRY physics-start guest="); p = pd_val(p, v->a); p = fmt_str(p, " key(grp)="); p = fmt_u64(p, v->b);
        if (v->a == NOTREAD) p = fmt_str(p, " (guest id pointer not on this thread's stack: not read)");
        break;
    case K_SOS:
        p = fmt_str(p, "ENTRY SOS-step guest="); p = pd_val(p, v->a);
        break;
    case K_RECOVER:
        p = fmt_str(p, "ENTRY recovery-receiver(msg 0) grp="); p = fmt_u64(p, v->a);
        p = fmt_str(p, " beh="); p = fmt_str(p, pd_beh_name(v->b));
        p = fmt_str(p, " +8="); p = fmt_u64(p, v->c); p = fmt_str(p, " +9="); p = fmt_u64(p, v->d);
        p = fmt_str(p, (v->e & 1) ? " => predicted PROCEED" : " => predicted SKIP");
        if (v->e & 2) p = fmt_str(p, " [more messages in this batch: not examined]");
        break;
    case K_EXIT:
        p = fmt_str(p, "ENTRY exit-behaviour grp="); p = pd_val(p, v->a);
        p = fmt_str(p, " +0x1b="); p = fmt_u64(p, v->b); p = fmt_str(p, " handle=");
        p = v->c == NOTREAD ? fmt_str(p, "(none)") : fmt_hex(p, v->c);
        break;
    case K_PURGE:
        p = fmt_str(p, "SANITY purge ids="); p = fmt_u64(p, v->a);
        if (v->a) { p = fmt_str(p, " first="); p = fmt_u64(p, v->b); }
        break;
    default:
        p = fmt_str(p, "?");
    }
    if (v->ret) {
        p = fmt_str(p, " | caller "); p = fmt_hex(p, v->ret);
        const char *c = pd_caller(v->ret);
        if (c) { p = fmt_str(p, " ("); p = fmt_str(p, c); *p++ = ')'; }
    }
    *p = 0;
    log_line(b);
}

static void pd_report(void)
{
    uint64_t head = g_pd_head;
    int printed = 0;
    uint64_t lost = 0, skipped = 0;
    if (head - g_pd_tail > PD_RING) { lost += head - g_pd_tail - PD_RING; g_pd_tail = head - PD_RING; }
    while (g_pd_tail < head) {
        uint64_t i = g_pd_tail;
        struct pd_ev *slot = &g_pd_ring[i & (PD_RING - 1)];
        uint64_t s1 = slot->seq;
        if (s1 == 0 || s1 < i + 1) break;
        __sync_synchronize();
        struct pd_ev v = *slot;
        __sync_synchronize();
        if (s1 != i + 1 || slot->seq != s1) { lost++; g_pd_tail++; continue; }
        if (printed < PD_PRINT_PER_REPORT && g_pd_printed < PD_PRINT_BUDGET) { pd_line(&v, i); printed++; g_pd_printed++; }
        else skipped++;
        g_pd_tail++;
    }
    if (lost || skipped) {
        char b[128], *p = b;
        p = fmt_str(p, "pd: events not printed (limit) "); p = fmt_u64(p, skipped);
        p = fmt_str(p, ", lost (ring overrun) "); p = fmt_u64(p, lost);
        *p = 0; log_line(b);
    }
}

/* ---- Lua entry points ---------------------------------------------------------------------- */
__declspec(dllexport) int irs_pd_build(void *L) { (void)L; return PD_BUILD; }
__declspec(dllexport) int irs_pd_enable(void *L) { (void)L; return pd_set(1); }
__declspec(dllexport) int irs_pd_disable(void *L) { (void)L; return pd_set(0); }
__declspec(dllexport) int irs_pd_status(void *L)
{
    (void)L;
    if (!pd_supported()) return ST_UNSUPPORTED;
    int st = pd_code_state();
    return st < 0 ? ST_RACE : (st ? ST_ON : ST_OFF);
}
__declspec(dllexport) int irs_pd_report(void *L) { (void)L; pd_report(); return 1; }

/* helper -> scripts: irs_pd_next returns 2 = none, 3 = group index, 4 = guest id; then 64 x irs_pd_bit (MSB first): 1 set, 2 clear */
static uint64_t g_pd_cur;
__declspec(dllexport) int irs_pd_next(void *L)
{
    (void)L;
    while (g_pd_items_tail < g_pd_items_head) {
        uint64_t i = g_pd_items_tail++;
        uint64_t v = __sync_lock_test_and_set(&g_pd_items[i & (PD_ITEMS - 1)], 0);
        if (!v) continue;
        g_pd_cur = v >> 2;
        return (v & 3) == 1 ? 3 : 4;
    }
    return 2;
}
__declspec(dllexport) int irs_pd_bit(void *L)
{
    (void)L;
    int b = (int)(g_pd_cur >> 63);
    g_pd_cur <<= 1;
    return b ? 1 : 2;
}

/* scripts -> helper: begin / bit0 / bit1 / push, then irs_pd_note */
#define PD_NOTE_MAX 8
static uint64_t g_pd_acc, g_pd_vals[PD_NOTE_MAX];
static int g_pd_nvals;
__declspec(dllexport) int irs_pd_begin(void *L) { (void)L; g_pd_acc = 0; g_pd_nvals = 0; return 1; }
__declspec(dllexport) int irs_pd_bit0(void *L) { (void)L; g_pd_acc <<= 1; return 1; }
__declspec(dllexport) int irs_pd_bit1(void *L) { (void)L; g_pd_acc = (g_pd_acc << 1) | 1; return 1; }
__declspec(dllexport) int irs_pd_push(void *L)
{
    (void)L;
    if (g_pd_nvals < PD_NOTE_MAX) g_pd_vals[g_pd_nvals++] = g_pd_acc;
    g_pd_acc = 0;
    return 1;
}
/* notes (script thread, public script functions):
 *  [1, guest, GetGuestGroupID+1 | 0]                     [2, grp, displayed behaviour+1 | 0]
 *  [3, nGuestsInvolved]                                  [4, trapped count]
 *  [5, grp, member count, up to 5 member guest ids]      */
__declspec(dllexport) int irs_pd_note(void *L)
{
    (void)L;
    if (g_pd_nvals < 1) return 2;
    uint64_t k = g_pd_vals[0], a = g_pd_nvals > 1 ? g_pd_vals[1] : 0, c = g_pd_nvals > 2 ? g_pd_vals[2] : 0;
    char b[320], *p = b;
    p = fmt_str(p, "pd t="); p = fmt_u64(p, GetTickCount64() - g_pd_t0); p = fmt_str(p, "ms tid="); p = fmt_u64(p, GetCurrentThreadId());
    switch (k) {
    case 1:
        p = fmt_str(p, " SCRIPT guest="); p = fmt_u64(p, a);
        if (c) { p = fmt_str(p, " GetGuestGroupID="); p = fmt_u64(p, c - 1); } else p = fmt_str(p, " GetGuestGroupID=(none/unreadable)");
        break;
    case 2:
        p = fmt_str(p, " STATE grp="); p = fmt_u64(p, a); p = fmt_str(p, " displayed behaviour now ");
        p = fmt_str(p, c ? pd_beh_name(c - 1) : "(unreadable)");
        break;
    case 3:
        p = fmt_str(p, " STATE message GuestPhysicsIncidentEnded received, nGuestsInvolved="); p = fmt_u64(p, a);
        break;
    case 4:
        p = fmt_str(p, " STATE trapped (SOS) guests now "); p = fmt_u64(p, a);
        break;
    case 5:
        p = fmt_str(p, " STATE grp="); p = fmt_u64(p, a); p = fmt_str(p, " members ("); p = fmt_u64(p, c); p = fmt_str(p, "):");
        for (int i = 3; i < g_pd_nvals; i++) { *p++ = ' '; p = fmt_u64(p, g_pd_vals[i]); }
        if (c + 3 > (uint64_t)g_pd_nvals) p = fmt_str(p, " ...");
        break;
    default:
        p = fmt_str(p, " note "); p = fmt_u64(p, k);
    }
    *p = 0; log_line(b);
    return 1;
}
