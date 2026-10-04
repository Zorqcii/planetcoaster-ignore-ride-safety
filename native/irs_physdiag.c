/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Zorqcii
 *
 * DIAGNOSTIC 0.2.0-diag.2: logging-only observation of the game's own guest-physics chain
 * (natural impact -> group request -> enter Physics -> per-guest launch -> recovery).
 * Built instead of irs_experimental.c (build variant IRS_PHYSDIAG): this build contains NO gameplay
 * patch (no open gate, no rating changes, no close skips), NO prototype/launch code, and the
 * safety and nausea options refuse to turn on.
 *
 * Ten entry hooks, each a 5-7 byte `jmp` over whole instructions at a function start. The wrapper
 * saves the argument registers, calls a C body that only READS memory the hooked routine itself
 * reads at that point, writes an event to a ring buffer, restores the registers and continues in
 * the original code through a trampoline. Nothing in the game is written. The log file is written
 * only from the script thread (irs_pd_report).
 *
 * Event labels in the log:
 *   ENTRY  - a hooked function was entered; values are as read at entry; outcomes are "predicted"
 *   STATE  - a confirmed state change observed later (guest entered/left the physics guest map,
 *            group key appeared/disappeared; script-side behaviour changes and messages)
 *   SANITY - the unloading routine (purge); a sanity check only, not proof that every crash was seen
 */

__declspec(dllimport) void *__stdcall VirtualAlloc(void *, uintptr_t, DWORD, DWORD);
__declspec(dllimport) uint64_t __stdcall GetTickCount64(void);
__declspec(dllimport) DWORD __stdcall GetCurrentThreadId(void);

int _fltused = 0;

#define PD_BUILD 3                  /* irs_pd_build: 3 = 0.2.0-diag.2 */

/* ---- formatting (no C runtime) ---------------------------------------------------------- */
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

/* ---- hook sites ----------------------------------------------------------------------------
 * n = length of the overwritten span = whole instructions (verified offline); patch = e9 rel32 to
 * the stub, then 1-byte (90) or 2-byte (66 90) nop fill. Every span lies in one aligned 8-byte word. */
struct pdsite { uint64_t va; int n; uint8_t orig[8]; uint8_t patch[8]; const char *name; };
enum { S_IMPACT, S_POST, S_REQUEST, S_ENTER, S_LAUNCH, S_UPDATE, S_SOS, S_RECOVER, S_EXIT, S_PURGE, PD_NSITES };
static struct pdsite g_pd_sites[PD_NSITES] = {
    {0x1406b58b0ULL, 6, {0x4c, 0x8b, 0xdc, 0x53, 0x41, 0x54}, {0}, "impact receiver"},          /* mov r11,rsp ; push rbx ; push r12 */
    {0x1406f0a00ULL, 5, {0x48, 0x89, 0x5c, 0x24, 0x10}, {0}, "post group request"},              /* mov [rsp+0x10],rbx */
    {0x1406a8bc0ULL, 6, {0x40, 0x53, 0x56, 0x57, 0x41, 0x56}, {0}, "request receiver"},          /* push rbx (40 53) ; push rsi ; push rdi ; push r14 */
    {0x14069c8a0ULL, 7, {0x4c, 0x8b, 0xdc, 0x4d, 0x89, 0x4b, 0x20}, {0}, "enter Physics"},       /* mov r11,rsp ; mov [r11+0x20],r9 */
    {0x14067d450ULL, 5, {0x4c, 0x89, 0x4c, 0x24, 0x20}, {0}, "physics start"},                   /* mov [rsp+0x20],r9 */
    {0x14067de10ULL, 5, {0x48, 0x8b, 0xc4, 0x55, 0x53}, {0}, "guest-physics update"},            /* mov rax,rsp ; push rbp ; push rbx */
    {0x140680af0ULL, 5, {0x48, 0x89, 0x5c, 0x24, 0x20}, {0}, "SOS step"},                        /* mov [rsp+0x20],rbx */
    {0x1406a8ed0ULL, 5, {0x48, 0x89, 0x5c, 0x24, 0x08}, {0}, "recovery receiver"},               /* mov [rsp+8],rbx */
    {0x14069cec0ULL, 5, {0x48, 0x89, 0x5c, 0x24, 0x20}, {0}, "exit behaviour"},                  /* mov [rsp+0x20],rbx */
    {0x14081a0a0ULL, 5, {0x48, 0x8b, 0xc4, 0x55, 0x53}, {0}, "purge (unloading)"},               /* mov rax,rsp ; push rbp ; push rbx */
};

static const struct fp g_pd_fps[] = {
    /* D1 impact receiver after entry .. message loop: guest +0x18, map +0x218/+0x220 (64-bit hash) */
    {0x1406b58b6ULL, 24, {0x41, 0x57, 0x48, 0x83, 0xec, 0x60, 0x48, 0x8b, 0x5a, 0x10, 0x4c, 0x8b, 0xf9, 0x48, 0x8b, 0x42, 0x08, 0x45, 0x33, 0xe4, 0x48, 0x8d, 0x04, 0xc3}},
    {0x1406b58ceULL, 24, {0x48, 0x8b, 0xc8, 0x48, 0x2b, 0xcb, 0x48, 0x83, 0xc1, 0x07, 0x48, 0xc1, 0xe9, 0x03, 0x48, 0x3b, 0xd8, 0x49, 0x0f, 0x47, 0xcc, 0x48, 0x89, 0x4c}},
    {0x1406b58e6ULL, 24, {0x24, 0x30, 0x48, 0x85, 0xc9, 0x0f, 0x84, 0x3e, 0x02, 0x00, 0x00, 0x49, 0x89, 0x73, 0xe0, 0x41, 0xb9, 0x42, 0x11, 0x00, 0x00, 0x49, 0x89, 0x6b}},
    {0x1406b58feULL, 18, {0x08, 0x49, 0x89, 0x7b, 0xd8, 0x4d, 0x89, 0x6b, 0xd0, 0x4d, 0x89, 0x73, 0xc8, 0x0f, 0x1f, 0x44, 0x00, 0x00}},
    /* D1: guest = [msg+0x18] ; hash ; div [r15+0x220] ; buckets [r15+0x218] ; node key +8, value +0x10 */
    {0x1406b5910ULL, 24, {0x48, 0x8b, 0x33, 0x48, 0x8b, 0x4e, 0x18, 0x48, 0x8b, 0xd1, 0x48, 0x8b, 0xc1, 0x48, 0xf7, 0xd0, 0x48, 0xc1, 0xe2, 0x12, 0x48, 0x03, 0xd0, 0x48}},
    {0x1406b5928ULL, 24, {0x8b, 0xc2, 0x48, 0xc1, 0xe8, 0x1f, 0x48, 0x33, 0xc2, 0x48, 0x6b, 0xd0, 0x15, 0x48, 0x8b, 0xc2, 0x48, 0xc1, 0xe8, 0x0b, 0x48, 0x33, 0xc2, 0x48}},
    {0x1406b5940ULL, 24, {0x6b, 0xd0, 0x41, 0x48, 0x8b, 0xc2, 0x8b, 0xd2, 0x48, 0xc1, 0xe8, 0x16, 0x8b, 0xc0, 0x48, 0x33, 0xc2, 0x33, 0xd2, 0x49, 0xf7, 0xb7, 0x20, 0x02}},
    {0x1406b5958ULL, 20, {0x00, 0x00, 0x49, 0x8b, 0x87, 0x18, 0x02, 0x00, 0x00, 0x4c, 0x8d, 0x04, 0xd0, 0x48, 0x8b, 0x04, 0xd0, 0x49, 0x3b, 0xc0}},
    /* D1: [r15+0x288][idx] == guest ; rec = [[r15+0x1d0]+0x3b8]+idx*0x250 ; +8 ; +0x1a mask ; +9 ; reason(+0x38)==2 && +0xd8 ; src(+0x40) vs +0x9a50 */
    {0x1406b5985ULL, 24, {0x48, 0x85, 0xc0, 0x0f, 0x84, 0x6d, 0x01, 0x00, 0x00, 0x44, 0x8b, 0x50, 0x10, 0x49, 0x8b, 0x87, 0x88, 0x02, 0x00, 0x00, 0x41, 0x8b, 0xd2, 0x44}},
    {0x1406b599dULL, 24, {0x89, 0x94, 0x24, 0x88, 0x00, 0x00, 0x00, 0x4a, 0x39, 0x0c, 0xd0, 0x0f, 0x85, 0x4d, 0x01, 0x00, 0x00, 0x49, 0x8b, 0xbf, 0xd0, 0x01, 0x00, 0x00}},
    {0x1406b59b5ULL, 24, {0x4c, 0x8d, 0x6e, 0x38, 0x4c, 0x8b, 0x5e, 0x40, 0x48, 0x69, 0xca, 0x50, 0x02, 0x00, 0x00, 0x48, 0x03, 0x8f, 0xb8, 0x03, 0x00, 0x00, 0x80, 0x79}},
    {0x1406b59cdULL, 24, {0x08, 0x00, 0x0f, 0x84, 0x26, 0x01, 0x00, 0x00, 0x0f, 0xb6, 0x41, 0x1a, 0x3c, 0x0c, 0x0f, 0x87, 0x1a, 0x01, 0x00, 0x00, 0x0f, 0xb6, 0xc0, 0x41}},
    {0x1406b59e5ULL, 24, {0x0f, 0xa3, 0xc1, 0x0f, 0x83, 0x0d, 0x01, 0x00, 0x00, 0x80, 0x79, 0x09, 0x00, 0x0f, 0x84, 0x03, 0x01, 0x00, 0x00, 0x41, 0x83, 0x7d, 0x00, 0x02}},
    {0x1406b59fdULL, 6, {0x75, 0x2d, 0x80, 0xb9, 0xd8, 0x00}},
    /* D1: pending map +0x250/+0x238/+0x240 (32-bit hash), node key dword +8 */
    {0x1406b5a39ULL, 24, {0x49, 0x83, 0xbf, 0x50, 0x02, 0x00, 0x00, 0x00, 0x74, 0x63, 0x41, 0x69, 0xc2, 0x01, 0x10, 0x00, 0x00, 0x33, 0xd2, 0x8b, 0xc8, 0xc1, 0xe9, 0x16}},
    {0x1406b5a51ULL, 24, {0x33, 0xc8, 0x6b, 0xc1, 0x11, 0x8b, 0xc8, 0xc1, 0xe9, 0x09, 0x33, 0xc8, 0x69, 0xc1, 0x01, 0x04, 0x00, 0x00, 0x8b, 0xc8, 0xc1, 0xe9, 0x02, 0x33}},
    {0x1406b5a69ULL, 24, {0xc8, 0x69, 0xc1, 0x81, 0x00, 0x00, 0x00, 0x8b, 0xc8, 0x48, 0xc1, 0xe8, 0x0c, 0x48, 0x33, 0xc1, 0x49, 0xf7, 0xb7, 0x40, 0x02, 0x00, 0x00, 0x49}},
    {0x1406b5a81ULL, 24, {0x8b, 0x87, 0x38, 0x02, 0x00, 0x00, 0x48, 0x8d, 0x0c, 0xd0, 0x48, 0x8b, 0x04, 0xd0, 0x48, 0x3b, 0xc1, 0x74, 0x12, 0x44, 0x3b, 0x50, 0x08, 0x0f}},
    {0x1406b5a99ULL, 13, {0x84, 0x9b, 0x00, 0x00, 0x00, 0x48, 0x8b, 0x00, 0x48, 0x3b, 0xc1, 0x75, 0xee}},
    /* D2 post request after entry: rdx=&group, r8, r9, stack args */
    {0x1406f0a05ULL, 24, {0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xec, 0x20, 0x8b, 0x05, 0x2f, 0x4e, 0x4e, 0x01, 0x48, 0x8b, 0xf2, 0x48, 0x8d, 0x54, 0x24, 0x30}},
    {0x1406f0a1dULL, 12, {0x89, 0x44, 0x24, 0x30, 0x49, 0x8b, 0xd9, 0x49, 0x8b, 0xf8, 0xe8, 0x04}},
    /* D2: reads [rdx] group, [stack+0x50] reason, [stack+0x58] source */
    {0x1406f0a40ULL, 24, {0xf3, 0x0f, 0x10, 0x43, 0x08, 0x0f, 0x12, 0x23, 0x0f, 0x28, 0xd0, 0x0f, 0x12, 0x1f, 0xf3, 0x0f, 0x10, 0x47, 0x08, 0x8b, 0x06, 0x48, 0x8b, 0x54}},
    {0x1406f0a58ULL, 24, {0x24, 0x50, 0x48, 0x8b, 0x4c, 0x24, 0x58, 0x0f, 0x16, 0xd8, 0x0f, 0x16, 0xe2, 0x44, 0x8b, 0x02, 0x48, 0x8d, 0x15, 0x71, 0xce, 0x2f, 0x01, 0x48}},
    {0x1406f0a70ULL, 24, {0x8b, 0x09, 0x49, 0x89, 0x12, 0x8b, 0x15, 0xc5, 0x4d, 0x4e, 0x01, 0x41, 0x89, 0x52, 0x08, 0x8b, 0x15, 0xbf, 0x4d, 0x4e, 0x01, 0x41, 0x89, 0x52}},
    {0x1406f0a88ULL, 24, {0x0c, 0x48, 0x8b, 0x15, 0xb8, 0x4d, 0x4e, 0x01, 0x49, 0x89, 0x52, 0x10, 0x48, 0x8d, 0x15, 0x35, 0xae, 0x2e, 0x01, 0x49, 0x89, 0x12, 0x41, 0x89}},
    {0x1406f0aa0ULL, 24, {0x42, 0x18, 0x49, 0x8b, 0xc2, 0x45, 0x89, 0x42, 0x1c, 0x41, 0x0f, 0x13, 0x5a, 0x20, 0x0f, 0x12, 0xdb, 0xf3, 0x41, 0x0f, 0x11, 0x5a, 0x28, 0x41}},
    {0x1406f0ab8ULL, 18, {0x0f, 0x13, 0x62, 0x2c, 0x0f, 0x12, 0xe4, 0xf3, 0x41, 0x0f, 0x11, 0x62, 0x34, 0x49, 0x89, 0x4a, 0x38, 0x48}},
    /* D3 request receiver after entry */
    {0x1406a8bc5ULL, 24, {0x56, 0x48, 0x83, 0xec, 0x38, 0x48, 0x8b, 0x5a, 0x10, 0x33, 0xff, 0x48, 0x8b, 0x42, 0x08, 0x48, 0x8b, 0xf1, 0x48, 0x8d, 0x04, 0xc3, 0x4c, 0x8b}},
    {0x1406a8bddULL, 12, {0xf0, 0x4c, 0x2b, 0xf3, 0x49, 0x83, 0xc6, 0x07, 0x49, 0xc1, 0xee, 0x03}},
    /* D3: msg +0x18 group, +0x1c reason, +0x38 source ; rec +8, +0x1a mask, +9, +0xd8 ; +0x9a50 */
    {0x1406a8c10ULL, 24, {0x48, 0x8b, 0x2b, 0x48, 0x8b, 0x86, 0xb8, 0x03, 0x00, 0x00, 0x44, 0x8b, 0x55, 0x18, 0x4c, 0x8b, 0x5d, 0x38, 0x4d, 0x69, 0xfa, 0x50, 0x02, 0x00}},
    {0x1406a8c28ULL, 24, {0x00, 0x41, 0x80, 0x7c, 0x07, 0x08, 0x00, 0x49, 0x8d, 0x0c, 0x07, 0x74, 0x6d, 0x0f, 0xb6, 0x41, 0x1a, 0x3c, 0x0c, 0x77, 0x65, 0x41, 0x0f, 0xa3}},
    {0x1406a8c40ULL, 24, {0xc4, 0x73, 0x5f, 0x80, 0x79, 0x09, 0x00, 0x74, 0x59, 0x83, 0x7d, 0x1c, 0x02, 0x75, 0x23, 0x80, 0xb9, 0xd8, 0x00, 0x00, 0x00, 0x01, 0x75, 0x1a}},
    {0x1406a8c58ULL, 24, {0x48, 0x8b, 0x8e, 0x58, 0x02, 0x00, 0x00, 0x48, 0x8d, 0x54, 0x24, 0x68, 0x44, 0x89, 0x54, 0x24, 0x68, 0xe8, 0x22, 0x53, 0x02, 0x00, 0x84, 0xc0}},
    {0x1406a8c70ULL, 14, {0x75, 0x30, 0x4c, 0x3b, 0x9e, 0x50, 0x9a, 0x00, 0x00, 0x74, 0x27, 0x4c, 0x8b, 0x86}},
    /* D3: call enter-Physics(rsi, &group, rec, msg+0x2c, reason) */
    {0x1406a8c7bULL, 24, {0x4c, 0x8b, 0x86, 0xb8, 0x03, 0x00, 0x00, 0x4c, 0x8d, 0x4d, 0x2c, 0x8b, 0x45, 0x1c, 0x48, 0x8d, 0x54, 0x24, 0x68, 0x4d, 0x03, 0xc7, 0x44, 0x89}},
    {0x1406a8c93ULL, 10, {0x54, 0x24, 0x68, 0x48, 0x8b, 0xce, 0x89, 0x44, 0x24, 0x20}},
    /* D4 enter-Physics after entry: rdx=&group, r8=rec, r9=vec3, reason [rsp+0xa0] ; members [rec],[rec+4] ; guest [[r15+0x3b0]+i*0x30+8] */
    {0x14069c8a7ULL, 24, {0x53, 0x55, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x50, 0x8b, 0x02, 0x49, 0x8b, 0xd9, 0x4c, 0x8b, 0xea, 0x41, 0x89, 0x43, 0x10}},
    {0x14069c8bfULL, 24, {0x45, 0x33, 0xc9, 0x49, 0x8d, 0x53, 0x10, 0x4d, 0x8b, 0xf0, 0x4c, 0x8b, 0xf9, 0xe8, 0xef, 0x05, 0x00, 0x00, 0x49, 0x83, 0x7e, 0x50, 0xff, 0x75}},
    {0x14069c8d7ULL, 24, {0x1c, 0x49, 0x8b, 0x87, 0x98, 0x01, 0x00, 0x00, 0xb9, 0x01, 0x00, 0x00, 0x00, 0xf0, 0x48, 0x0f, 0xc1, 0x88, 0xb0, 0x00, 0x00, 0x00, 0x48, 0xff}},
    {0x14069c8efULL, 21, {0xc1, 0x49, 0x89, 0x4e, 0x50, 0x41, 0x8b, 0x2e, 0x41, 0x3b, 0x6e, 0x04, 0x0f, 0x84, 0x07, 0x01, 0x00, 0x00, 0x48, 0x89, 0xb4}},
    /* D4: member guest entity from [rdi+8] */
    {0x14069c983ULL, 16, {0x49, 0x8b, 0x5e, 0x50, 0x48, 0x8b, 0x7f, 0x08, 0x4d, 0x8b, 0xa7, 0xa0, 0x01, 0x00, 0x00, 0xe8}},
    /* D4: mov word [r14+0x1a],0x10b */
    {0x14069ca08ULL, 7, {0x66, 0x41, 0xc7, 0x46, 0x1a, 0x0b, 0x01}},
    /* D5 physics start after entry */
    {0x14067d455ULL, 22, {0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x48, 0x4c, 0x8b, 0xe2, 0x48, 0x8b, 0xe9}},
    /* D5: mov rbx,r8 */
    {0x14067d475ULL, 3, {0x49, 0x8b, 0xd8}},
    /* D5: mov edx,[rbx] (group key) */
    {0x14067d48fULL, 2, {0x8b, 0x13}},
    /* D5: mov rcx,[r12] (guest id) */
    {0x14067d4a0ULL, 4, {0x49, 0x8b, 0x0c, 0x24}},
    /* D6 guest-physics update after entry .. mov r14,rcx */
    {0x14067de15ULL, 24, {0x41, 0x56, 0x48, 0x8d, 0xa8, 0x68, 0xf6, 0xff, 0xff, 0x48, 0x81, 0xec, 0x80, 0x0a, 0x00, 0x00, 0x0f, 0x29, 0x78, 0x98, 0x4c, 0x8b, 0xf1, 0x48}},
    /* start: add rcx,0x248 (group map) */
    {0x14067d46bULL, 7, {0x48, 0x81, 0xc1, 0x48, 0x02, 0x00, 0x00}},
    /* start: lea rcx,[rbp+0x228] (guest map) */
    {0x14067d47dULL, 7, {0x48, 0x8d, 0x8d, 0x28, 0x02, 0x00, 0x00}},
    /* start: inc [rbp+0x288] ; timer [rbp+0x290] */
    {0x14067d6f0ULL, 19, {0x48, 0xff, 0x85, 0x88, 0x02, 0x00, 0x00, 0xb0, 0x01, 0xc7, 0x85, 0x90, 0x02, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x40}},
    /* guest map lookup (re-implemented read-only) */
    {0x1400c6f70ULL, 24, {0x4c, 0x8b, 0x02, 0x48, 0x8b, 0xf1, 0x4d, 0x8b, 0xc8, 0x49, 0x8b, 0xc0, 0x48, 0xf7, 0xd0, 0x49, 0xc1, 0xe1, 0x12, 0x4c, 0x03, 0xc8, 0x4c, 0x8b}},
    {0x1400c6f88ULL, 24, {0xf2, 0x49, 0x8b, 0xc1, 0x33, 0xd2, 0x48, 0xc1, 0xe8, 0x1f, 0x49, 0x33, 0xc1, 0x48, 0x6b, 0xc8, 0x15, 0x48, 0x8b, 0xc1, 0x48, 0xc1, 0xe8, 0x0b}},
    {0x1400c6fa0ULL, 24, {0x48, 0x33, 0xc1, 0x48, 0x6b, 0xc8, 0x41, 0x48, 0x8b, 0xc1, 0x8b, 0xc9, 0x48, 0xc1, 0xe8, 0x16, 0x8b, 0xc0, 0x48, 0x33, 0xc1, 0x48, 0xf7, 0x76}},
    {0x1400c6fb8ULL, 24, {0x08, 0x48, 0x8b, 0x06, 0x48, 0x8d, 0x0c, 0xd0, 0x48, 0x8b, 0x04, 0xd0, 0x48, 0x8b, 0xf9, 0x48, 0x3b, 0xc1, 0x74, 0x19, 0x0f, 0x1f, 0x40, 0x00}},
    {0x1400c6fd0ULL, 21, {0x4c, 0x3b, 0x40, 0x08, 0x0f, 0x84, 0xe3, 0x00, 0x00, 0x00, 0x48, 0x8b, 0xf8, 0x48, 0x8b, 0x00, 0x48, 0x3b, 0xc1, 0x75, 0xeb}},
    /* group map lookup (re-implemented read-only) */
    {0x1406e9120ULL, 24, {0x44, 0x8b, 0x02, 0x48, 0x8b, 0xf1, 0x41, 0x69, 0xc0, 0x01, 0x10, 0x00, 0x00, 0x4c, 0x8b, 0xf2, 0x33, 0xd2, 0x44, 0x8b, 0xc8, 0x41, 0xc1, 0xe9}},
    {0x1406e9138ULL, 24, {0x16, 0x44, 0x33, 0xc8, 0x41, 0x6b, 0xc1, 0x11, 0x44, 0x8b, 0xc8, 0x41, 0xc1, 0xe9, 0x09, 0x44, 0x33, 0xc8, 0x41, 0x69, 0xc1, 0x01, 0x04, 0x00}},
    {0x1406e9150ULL, 24, {0x00, 0x8b, 0xc8, 0xc1, 0xe9, 0x02, 0x33, 0xc8, 0x69, 0xc1, 0x81, 0x00, 0x00, 0x00, 0x8b, 0xc8, 0x48, 0xc1, 0xe8, 0x0c, 0x48, 0x33, 0xc1, 0x48}},
    {0x1406e9168ULL, 24, {0xf7, 0x76, 0x08, 0x48, 0x8b, 0x06, 0x48, 0x8d, 0x0c, 0xd0, 0x48, 0x8b, 0x04, 0xd0, 0x48, 0x8b, 0xf9, 0x48, 0x3b, 0xc1, 0x74, 0x17, 0x66, 0x90}},
    {0x1406e9180ULL, 21, {0x44, 0x3b, 0x40, 0x10, 0x0f, 0x84, 0xe3, 0x00, 0x00, 0x00, 0x48, 0x8b, 0xf8, 0x48, 0x8b, 0x00, 0x48, 0x3b, 0xc1, 0x75, 0xeb}},
    /* D7 SOS step after entry: r9=&guest */
    {0x140680af5ULL, 24, {0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0x6c, 0x24, 0xf9, 0x48, 0x81, 0xec, 0xd0, 0x00, 0x00, 0x00, 0x48}},
    {0x140680b0dULL, 24, {0x8b, 0x99, 0xf0, 0x01, 0x00, 0x00, 0x4c, 0x8b, 0xf1, 0x48, 0x8b, 0x45, 0x67, 0x4d, 0x8b, 0xf8, 0x4d, 0x8b, 0xe9, 0x4c, 0x8b, 0xe2, 0x4c, 0x8b}},
    /* D7 caller: r9 = &[rbp+0x160] = guest id */
    {0x14067fc9cULL, 24, {0x4d, 0x8b, 0xc4, 0x48, 0x8d, 0x45, 0xc8, 0x48, 0x89, 0x9d, 0x60, 0x01, 0x00, 0x00, 0x49, 0x8b, 0xd7, 0x48, 0x89, 0x44, 0x24, 0x20, 0x49, 0x8b}},
    {0x14067fcb4ULL, 6, {0xce, 0xe8, 0x36, 0x0e, 0x00, 0x00}},
    /* D8 recovery receiver after entry */
    {0x1406a8ed5ULL, 24, {0x48, 0x89, 0x6c, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x5a, 0x10, 0x33, 0xff, 0x48, 0x8b, 0x42}},
    {0x1406a8eedULL, 8, {0x08, 0x48, 0x8b, 0xe9, 0x48, 0x8d, 0x04, 0xc3}},
    /* D8: msg +0x18 group ; rec = [rbp+0x3b8]+idx*0x250 ; +0x1a == 0x0b ; +8 ; +9 */
    {0x1406a8f10ULL, 24, {0x48, 0x8b, 0x03, 0x8b, 0x40, 0x18, 0x4c, 0x69, 0xc0, 0x50, 0x02, 0x00, 0x00, 0x4c, 0x03, 0x85, 0xb8, 0x03, 0x00, 0x00, 0x41, 0x80, 0x78, 0x1a}},
    {0x1406a8f28ULL, 22, {0x0b, 0x75, 0x22, 0x41, 0x80, 0x78, 0x08, 0x00, 0x74, 0x1b, 0x41, 0x80, 0x78, 0x09, 0x00, 0x74, 0x14, 0x45, 0x33, 0xc9, 0x89, 0x44}},
    /* D9 exit-behaviour after entry: r8 = record ; [r8+0x1b] ; [r8+0x50] */
    {0x14069cec5ULL, 24, {0x55, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x48, 0x8b, 0xec, 0x48, 0x83, 0xec, 0x60, 0x41, 0x80, 0x78, 0x1b, 0x00, 0x45, 0x0f, 0xb6, 0xf1, 0x49}},
    {0x14069ceddULL, 24, {0x8b, 0xf0, 0x4c, 0x8b, 0xea, 0x48, 0x8b, 0xf9, 0x74, 0x35, 0x49, 0x83, 0x78, 0x50, 0xff, 0x74, 0x2a, 0x48, 0x8b, 0x89, 0x98, 0x01, 0x00, 0x00}},
    {0x14069cef5ULL, 2, {0x49, 0x8d}},
    /* D9: behaviour switch on [rsi+0x1a] */
    {0x14069cf9fULL, 24, {0x0f, 0xb6, 0x46, 0x1a, 0x48, 0x8d, 0x15, 0x56, 0x30, 0x96, 0xff, 0x4c, 0x89, 0xbc, 0x24, 0xa0, 0x00, 0x00, 0x00, 0xc6, 0x86, 0x4e, 0x02, 0x00}},
    {0x14069cfb7ULL, 2, {0x00, 0x00}},
    /* D10 purge after entry */
    {0x14081a0a5ULL, 24, {0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8d, 0xa8, 0x18, 0xfd, 0xff, 0xff, 0x48, 0x81, 0xec, 0xa8, 0x03, 0x00, 0x00}},
    /* D10: mov rsi,[rdx+0x18] */
    {0x14081a0bdULL, 4, {0x48, 0x8b, 0x72, 0x18}},
    /* D10: mov rax,[rdx+0x10] */
    {0x14081a0faULL, 4, {0x48, 0x8b, 0x42, 0x10}},
};

/* ---- event ring (hooks write, script thread reads) --------------------------------------- */
enum {
    K_IMPACT = 1, K_POST, K_REQUEST, K_ENTER, K_MEMBER, K_LAUNCH, K_GUESTMAP, K_GROUPKEY, K_SOS,
    K_RECOVER, K_EXITPHYS, K_EXITMEMBER, K_PURGE, K_UNTRACKED, K_KINDS
};
struct pd_ev { volatile uint64_t seq; uint32_t kind, tid; uint64_t t, ret, a, b, c, d, e; };
#define PD_RING 1024                /* power of two */
static struct pd_ev g_pd_ring[PD_RING];
static volatile uint64_t g_pd_head;
static uint64_t g_pd_tail;          /* script thread */
static volatile long g_pd_counts[K_KINDS];
static uint64_t g_pd_t0;
static long g_pd_printed;
#define PD_PRINT_BUDGET 5000
#define PD_PRINT_PER_REPORT 64
static volatile int g_pd_on;

static void pd_event(uint32_t kind, uint64_t ret, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t e)
{
    if (!g_pd_on || kind >= K_KINDS) return;
    __sync_fetch_and_add(&g_pd_counts[kind], 1);
    uint64_t i = __sync_fetch_and_add(&g_pd_head, 1);
    struct pd_ev *v = &g_pd_ring[i & (PD_RING - 1)];
    v->seq = 0;
    __sync_synchronize();
    v->kind = kind; v->tid = GetCurrentThreadId(); v->t = GetTickCount64(); v->ret = ret;
    v->a = a; v->b = b; v->c = c; v->d = d; v->e = e;
    __sync_synchronize();
    v->seq = i + 1;
}

/* ---- ids handed to the scripts (groups and guests to cross-check) ------------------------- */
#define PD_ITEMS 128
static volatile uint64_t g_pd_items[PD_ITEMS];   /* value << 2 | kind (1 group, 2 guest); 0 = empty */
static volatile uint64_t g_pd_items_head;
static uint64_t g_pd_items_tail;
static void pd_item(uint64_t kind, uint64_t value)
{
    if (!g_pd_on || value >> 61) return;
    uint64_t i = __sync_fetch_and_add(&g_pd_items_head, 1);
    g_pd_items[i & (PD_ITEMS - 1)] = (value << 2) | kind;
}

/* ---- read-only lookups with the game's hash functions and node layouts (fingerprinted) ---- */
/* buckets at m[0], bucket count m[1]; node: next [+0], 64-bit key [+8]; returns node or 0 */
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
/* 32-bit key at node + keyoff */
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

#define RD8(p)  (*(const uint8_t *)(uintptr_t)(p))
#define RD32(p) (*(const uint32_t *)(uintptr_t)(p))
#define RD64(p) (*(const uint64_t *)(uintptr_t)(p))

/* behaviours accepted by the entry rule (bit mask 0x1142: Navigating, AtShop, AtEntertainer, AtSecurityGuard) */
static int pd_beh_ok(uint8_t beh) { return beh <= 0xc && ((0x1142u >> beh) & 1); }

/* packed entry-check values: f8 | f9<<8 | beh<<16 | d8<<24 | srcEq<<32 | pending<<33(2 bits) | pred<<36(2) | found<<38 | leader<<39 */
enum { PRED_REJECT = 0, PRED_ACCEPT = 1, PRED_EXTRA = 2, PRED_NA = 3 };

/* ---- hook bodies: regs [0]=r9 [1]=r8 [2]=rdx [3]=rcx [4]=return address [9]=5th arg [10]=6th arg ---- */
#define PD_MAX_MSG 8
void pd_b_impact(const uint64_t *r)
{
    uint64_t sys = r[3], vec = r[2];
    if (!sys || !vec) return;
    uint64_t data = RD64(vec + 0x10), cnt = RD64(vec + 8);
    for (uint64_t i = 0; i < cnt && i < PD_MAX_MSG; i++) {
        uint64_t msg = RD64(data + 8 * i);
        if (!msg) continue;
        uint64_t guest = RD64(msg + 0x18), reason = RD32(msg + 0x38), src = RD64(msg + 0x40);
        uint64_t idx = 0xffffffffULL, packed = (uint64_t)PRED_NA << 36;
        const uint64_t *node = pd_find64((const uint64_t *)(uintptr_t)(sys + 0x218), guest);
        if (node) {
            idx = RD32((uint64_t)(uintptr_t)node + 0x10);
            packed = 1ULL << 38;
            if (RD64(RD64(sys + 0x288) + idx * 8) == guest) {
                packed |= 1ULL << 39;
                uint64_t gm = RD64(sys + 0x1d0);
                uint64_t rec = RD64(gm + 0x3b8) + idx * 0x250;
                uint8_t f8 = RD8(rec + 8), beh = 0, f9 = 0, d8 = 0;
                int pred = PRED_REJECT, srcEq = 0, pend = 0;
                if (f8) {
                    beh = RD8(rec + 0x1a);
                    if (pd_beh_ok(beh)) {
                        f9 = RD8(rec + 9);
                        if (f9) {
                            int extra = 0;
                            if (reason == 2) { d8 = RD8(rec + 0xd8); extra = d8 == 1; }
                            srcEq = src == RD64(gm + 0x9a50);
                            if (!srcEq) {
                                if (RD64(sys + 0x250)) pend = pd_find32((const uint64_t *)(uintptr_t)(sys + 0x238), (uint32_t)idx, 8) ? 1 : 0;
                                else pend = 2;
                                pred = extra ? PRED_EXTRA : (pend == 1 ? PRED_REJECT : PRED_ACCEPT);
                            }
                        }
                    }
                }
                packed |= (uint64_t)f8 | ((uint64_t)f9 << 8) | ((uint64_t)beh << 16) | ((uint64_t)d8 << 24) |
                          ((uint64_t)srcEq << 32) | ((uint64_t)pend << 33) | ((uint64_t)pred << 36);
            }
        }
        pd_event(K_IMPACT, r[4], guest, idx, reason, src, packed);
        pd_item(2, guest);
    }
}

void pd_b_post(const uint64_t *r)
{
    uint64_t idx = r[2] ? RD32(r[2]) : 0xffffffffULL;
    uint64_t reason = r[9] ? RD32(r[9]) : 0, src = r[10] ? RD64(r[10]) : 0;
    pd_event(K_POST, r[4], idx, reason, src, 0, 0);
}

void pd_b_request(const uint64_t *r)
{
    uint64_t gm = r[3], vec = r[2];
    if (!gm || !vec) return;
    uint64_t data = RD64(vec + 0x10), cnt = RD64(vec + 8);
    for (uint64_t i = 0; i < cnt && i < PD_MAX_MSG; i++) {
        uint64_t msg = RD64(data + 8 * i);
        if (!msg) continue;
        uint64_t idx = RD32(msg + 0x18), reason = RD32(msg + 0x1c), src = RD64(msg + 0x38);
        uint64_t rec = RD64(gm + 0x3b8) + idx * 0x250;
        uint8_t f8 = RD8(rec + 8), beh = 0, f9 = 0, d8 = 0;
        int pred = PRED_REJECT, srcEq = 0;
        if (f8) {
            beh = RD8(rec + 0x1a);
            if (pd_beh_ok(beh)) {
                f9 = RD8(rec + 9);
                if (f9) {
                    int extra = 0;
                    if (reason == 2) { d8 = RD8(rec + 0xd8); extra = d8 == 1; }
                    srcEq = src == RD64(gm + 0x9a50);
                    if (!srcEq) pred = extra ? PRED_EXTRA : PRED_ACCEPT;
                }
            }
        }
        uint64_t packed = (uint64_t)f8 | ((uint64_t)f9 << 8) | ((uint64_t)beh << 16) | ((uint64_t)d8 << 24) |
                          ((uint64_t)srcEq << 32) | ((uint64_t)pred << 36);
        pd_event(K_REQUEST, r[4], idx, reason, src, 0, packed);
    }
}

#define PD_MAX_MEMBERS 16
void pd_b_enter(const uint64_t *r)
{
    uint64_t gm = r[3], rec = r[1];
    if (!gm || !rec || !r[2]) return;
    uint64_t idx = RD32(r[2]);
    uint32_t first = RD32(rec), end = RD32(rec + 4);
    uint64_t reason = (uint32_t)r[9];          /* 5th argument passed by value */
    pd_event(K_ENTER, r[4], idx, RD8(rec + 0x1a), reason, (uint64_t)(end - first), first);
    pd_item(1, idx);
    uint64_t arr = RD64(gm + 0x3b0);
    for (uint32_t m = first, k = 0; m != end && k < PD_MAX_MEMBERS; m++, k++)
        pd_event(K_MEMBER, r[4], idx, RD64(arr + (uint64_t)m * 0x30 + 8), k, 0, 0);
}

/* tracking of launched guests for confirmed state changes (written by launch hook, read by update hook) */
#define PD_TRACK 64
struct pd_track { volatile long used; uint64_t guest; uint32_t key; int in_map, key_present; uint64_t since, left_at; };
static struct pd_track g_pd_track[PD_TRACK];

void pd_b_launch(const uint64_t *r)
{
    uint64_t guest = r[2] ? RD64(r[2]) : 0, key = r[1] ? RD32(r[1]) : 0xffffffffULL;
    pd_event(K_LAUNCH, r[4], guest, key, 0, 0, 0);
    pd_item(2, guest);
    for (int i = 0; i < PD_TRACK; i++) {
        struct pd_track *t = &g_pd_track[i];
        if (t->used == 2 && t->guest == guest) return;
    }
    for (int i = 0; i < PD_TRACK; i++) {
        struct pd_track *t = &g_pd_track[i];
        if (__sync_bool_compare_and_swap(&t->used, 0, 1)) {
            t->guest = guest; t->key = (uint32_t)key; t->in_map = -2; t->key_present = -2;
            t->since = GetTickCount64(); t->left_at = 0;
            __sync_synchronize();
            t->used = 2;
            return;
        }
    }
    pd_event(K_UNTRACKED, r[4], guest, key, 0, 0, 0);
}

void pd_b_update(const uint64_t *r)
{
    uint64_t sys = r[3];
    if (!sys) return;
    uint64_t now = GetTickCount64();
    for (int i = 0; i < PD_TRACK; i++) {
        struct pd_track *t = &g_pd_track[i];
        if (t->used != 2) continue;
        const uint64_t *gnode = pd_find64((const uint64_t *)(uintptr_t)(sys + 0x228), t->guest);
        int in = gnode ? 1 : 0;
        int kp = pd_find32((const uint64_t *)(uintptr_t)(sys + 0x248), t->key, 0x10) ? 1 : 0;
        if (in != t->in_map) {
            pd_event(K_GUESTMAP, 0, t->guest, (uint64_t)in, t->key, RD32(sys + 0x290), RD64(sys + 0x288));
            t->in_map = in;
            if (!in) t->left_at = now;
        }
        if (kp != t->key_present) {
            pd_event(K_GROUPKEY, 0, t->key, (uint64_t)kp, t->guest, 0, 0);
            t->key_present = kp;
        }
        /* stop tracking 30 s after the guest left the physics guest map, or after 10 minutes */
        if ((t->left_at && now - t->left_at > 30000) || now - t->since > 600000) t->used = 0;
    }
}

void pd_b_sos(const uint64_t *r)
{
    pd_event(K_SOS, r[4], r[0] ? RD64(r[0]) : 0, 0, 0, 0, 0);
}

void pd_b_recover(const uint64_t *r)
{
    uint64_t gm = r[3], vec = r[2];
    if (!gm || !vec) return;
    uint64_t data = RD64(vec + 0x10), cnt = RD64(vec + 8);
    for (uint64_t i = 0; i < cnt && i < PD_MAX_MSG; i++) {
        uint64_t msg = RD64(data + 8 * i);
        if (!msg) continue;
        uint64_t idx = RD32(msg + 0x18);
        uint64_t rec = RD64(gm + 0x3b8) + idx * 0x250;
        uint8_t beh = RD8(rec + 0x1a), f8 = 0, f9 = 0;
        int proceed = 0;
        if (beh == 0x0b) { f8 = RD8(rec + 8); if (f8) { f9 = RD8(rec + 9); proceed = f9 != 0; } }
        pd_event(K_RECOVER, r[4], idx, beh, f8, f9, (uint64_t)proceed);
    }
}

void pd_b_exit(const uint64_t *r)
{
    uint64_t gm = r[3], rec = r[1];
    if (!gm || !rec || !r[2]) return;
    if (RD8(rec + 0x1a) != 0x0b) return;             /* only groups leaving Physics */
    uint64_t idx = RD32(r[2]);
    uint32_t first = RD32(rec), end = RD32(rec + 4);
    pd_event(K_EXITPHYS, r[4], idx, (uint64_t)(end - first), RD8(rec + 0x1b), RD64(rec + 0x50), r[0] & 0xff);
    uint64_t arr = RD64(gm + 0x3b0);
    for (uint32_t m = first, k = 0; m != end && k < PD_MAX_MEMBERS; m++, k++)
        pd_event(K_EXITMEMBER, r[4], idx, RD64(arr + (uint64_t)m * 0x30 + 8), k, 0, 0);
}

void pd_b_purge(const uint64_t *r)
{
    uint64_t v = r[2], n = 0, first = 0;
    if (v) {
        n = RD64(v + 0x10);
        uint64_t data = RD64(v + 0x18);
        if (data && n > 0) first = RD64(data);
    }
    pd_event(K_PURGE, r[4], n, first, 0, 0, 0);
}

/* ---- wrappers ----------------------------------------------------------------------------
 * Entered by `jmp` from the patched entry, so rsp is exactly the entry rsp ([rsp] = return address,
 * 8 mod 16). Saves rcx, rdx, r8, r9 (argument registers) and xmm0-xmm3; the C body may change only
 * the volatile registers rax, r10, r11, xmm4, xmm5 and the flags, none of which the hooked routines
 * read before writing (verified offline). Stack after the pushes and sub is 16-byte aligned for the call. */
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
PD_W(3, pd_w_enter, pd_b_enter);
PD_W(4, pd_w_launch, pd_b_launch);
PD_W(5, pd_w_update, pd_b_update);
PD_W(6, pd_w_sos, pd_b_sos);
PD_W(7, pd_w_recover, pd_b_recover);
PD_W(8, pd_w_exit, pd_b_exit);
PD_W(9, pd_w_purge, pd_b_purge);

/* ---- stub page: per site i, stub at 0x40*i (jmp [rip+0] -> wrapper), trampoline at 0x40*i+0x20
 * (original n bytes, then jmp [rip+0] -> site+n). Pure function of its inputs (tested offline). */
static void pd_put_abs_jmp(uint8_t *p, uint64_t target)
{
    p[0] = 0xff; p[1] = 0x25; p[2] = p[3] = p[4] = p[5] = 0;
    for (int i = 0; i < 8; i++) p[6 + i] = (uint8_t)(target >> (8 * i));
}
static int pd_build_page(uint8_t *page, uint64_t page_va, const uint64_t *wrappers, struct pdsite *sites, int nsites)
{
    for (int i = 0; i < nsites; i++) {
        struct pdsite *s = &sites[i];
        uint8_t *stub = page + 0x40 * i, *tr = stub + 0x20;
        pd_put_abs_jmp(stub, wrappers[i]);
        for (int k = 0; k < s->n; k++) tr[k] = s->orig[k];
        pd_put_abs_jmp(tr + s->n, s->va + (uint64_t)s->n);
        int64_t rel = (int64_t)(page_va + 0x40 * (uint64_t)i) - (int64_t)(s->va + 5);
        if (rel > 0x7fffffffLL || rel < -0x80000000LL) return 0;
        s->patch[0] = 0xe9;
        for (int k = 0; k < 4; k++) s->patch[1 + k] = (uint8_t)((uint64_t)rel >> (8 * k));
        if (s->n == 6) s->patch[5] = 0x90;
        else if (s->n == 7) { s->patch[5] = 0x66; s->patch[6] = 0x90; }
        else if (s->n != 5) return 0;
    }
    return 1;
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
        if ((s->va & 7) + (uint64_t)s->n > 8) return 0;
        if (!mem_eq((const uint8_t *)(uintptr_t)s->va, s->orig, s->n)) return 0;
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
        (uint64_t)(uintptr_t)&pd_w_enter, (uint64_t)(uintptr_t)&pd_w_launch, (uint64_t)(uintptr_t)&pd_w_update,
        (uint64_t)(uintptr_t)&pd_w_sos, (uint64_t)(uintptr_t)&pd_w_recover, (uint64_t)(uintptr_t)&pd_w_exit,
        (uint64_t)(uintptr_t)&pd_w_purge};
    if (!pd_build_page(page, (uint64_t)(uintptr_t)page, w, g_pd_sites, PD_NSITES)) {
        log_line("diag: stub page out of range");
        return 0;                                  /* page stays allocated but unused */
    }
    for (int i = 0; i < PD_NSITES; i++) g_pd_tramp[i] = page + 0x40 * i + 0x20;
    FlushInstructionCache(GetCurrentProcess(), page, 0x40 * PD_NSITES);
    __sync_synchronize();
    g_pd_page = page;
    return 1;
}

/* 0 all original, 1 all patched, -1 mixed/unknown */
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
    if (cur < 0) { log_line("diag: hook sites in an unexpected state - not changed"); return ST_RACE; }
    if (on) {                                     /* logging active before the first hook can fire */
        if (!g_pd_t0) g_pd_t0 = GetTickCount64();
        g_pd_on = 1;
    }
    /* install in order; remove in reverse order; roll back on any failure */
    int done = 0, err = 0;
    for (; done < PD_NSITES; done++) {
        int i = on ? done : PD_NSITES - 1 - done;
        const struct pdsite *s = &g_pd_sites[i];
        int r = swap_bytes(s->va, s->n, on ? s->orig : s->patch, on ? s->patch : s->orig);
        if (r != 1) { err = r < 0 ? ST_PROTECT_FAIL : ST_RACE; break; }
    }
    if (err) {
        for (int k = done - 1; k >= 0; k--) {
            int i = on ? k : PD_NSITES - 1 - k;
            const struct pdsite *s = &g_pd_sites[i];
            swap_bytes(s->va, s->n, on ? s->patch : s->orig, on ? s->orig : s->patch);
        }
        log_line("diag: hook change failed and was rolled back");
        if (on) g_pd_on = 0;
        return err;
    }
    int st = pd_code_state();
    if (on) {
        log_line(st == 1 ? "diag: 10 observation hooks installed and read back OK (no gameplay patches in this build)"
                         : "diag: READ-BACK FAILED after install");
    } else {
        g_pd_on = 0;      /* events already in the ring are still written by the next report */
        log_line(st == 0 ? "diag: hooks removed, original code read back OK" : "diag: READ-BACK FAILED after removal");
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
    case 0x1406a8ca2ULL: return "request receiver";
    case 0x14408ee69ULL: return "protected caller 0x14408ee20";
    case 0x14067fcbaULL: return "guest-physics update";
    case 0x14069c8d1ULL: return "enter Physics";
    case 0x14081bb14ULL: return "destroyed-vehicle listener";
    case 0x14046c09eULL: return "script rides:PurgeAllRideGuests";
    case 0x140856a8bULL: return "station purge (0x140856a40)";
    case 0x1405d4041ULL: case 0x1405d4088ULL: return "train-removed handler";
    default: return 0;
    }
}
static char *pd_checks(char *p, uint64_t e)
{
    static const char *pred[] = {"predicted REJECT", "predicted ACCEPT", "needs extra check 0x1406cdf90 (not evaluated)", "n/a"};
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
    char b[400], *p = b;
    p = fmt_str(p, "pd #"); p = fmt_u64(p, idx + 1);
    p = fmt_str(p, " t="); p = fmt_u64(p, v->t - g_pd_t0); p = fmt_str(p, "ms tid="); p = fmt_u64(p, v->tid); *p++ = ' ';
    switch (v->kind) {
    case K_IMPACT:
        p = fmt_str(p, "ENTRY impact-event guest="); p = fmt_u64(p, v->a);
        if (v->b == 0xffffffffULL) p = fmt_str(p, " grp=(not in guest->group map)");
        else { p = fmt_str(p, " grp="); p = fmt_u64(p, v->b); }
        p = fmt_str(p, " reason="); p = fmt_u64(p, v->c); p = fmt_str(p, " src="); p = fmt_hex(p, v->d);
        if (((v->e >> 36) & 3) == PRED_NA && !((v->e >> 38) & 1)) p = fmt_str(p, " => predicted REJECT (no group)");
        else if (!((v->e >> 39) & 1)) p = fmt_str(p, " => predicted REJECT (per-group entry mismatch)");
        else {
            p = pd_checks(p, v->e);
            uint64_t pend = (v->e >> 33) & 3;
            p = fmt_str(p, pend == 1 ? " (request already pending)" : pend == 2 ? " (no pending requests)" : "");
        }
        break;
    case K_POST:
        p = fmt_str(p, "ENTRY post-group-request grp="); p = fmt_u64(p, v->a);
        p = fmt_str(p, " reason="); p = fmt_u64(p, v->b); p = fmt_str(p, " src="); p = fmt_hex(p, v->c);
        break;
    case K_REQUEST:
        p = fmt_str(p, "ENTRY request-receiver grp="); p = fmt_u64(p, v->a);
        p = fmt_str(p, " reason="); p = fmt_u64(p, v->b); p = fmt_str(p, " src="); p = fmt_hex(p, v->c);
        p = pd_checks(p, v->e);
        break;
    case K_ENTER:
        p = fmt_str(p, "ENTRY enter-Physics grp="); p = fmt_u64(p, v->a);
        p = fmt_str(p, " previous beh="); p = fmt_str(p, pd_beh_name(v->b));
        p = fmt_str(p, " reason="); p = fmt_u64(p, v->c); p = fmt_str(p, " members="); p = fmt_u64(p, v->d);
        break;
    case K_MEMBER:
        p = fmt_str(p, "ENTRY enter-Physics grp="); p = fmt_u64(p, v->a);
        p = fmt_str(p, " member#"); p = fmt_u64(p, v->c); p = fmt_str(p, " guest="); p = fmt_u64(p, v->b);
        break;
    case K_LAUNCH:
        p = fmt_str(p, "ENTRY physics-start guest="); p = fmt_u64(p, v->a); p = fmt_str(p, " key(grp)="); p = fmt_u64(p, v->b);
        break;
    case K_GUESTMAP:
        p = fmt_str(p, v->b ? "STATE guest ENTERED physics guest map guest=" : "STATE guest LEFT physics guest map guest=");
        p = fmt_u64(p, v->a); p = fmt_str(p, " key(grp)="); p = fmt_u64(p, v->c);
        p = fmt_str(p, " timer-bits="); p = fmt_hex(p, v->d); p = fmt_str(p, " incident-counter="); p = fmt_u64(p, v->e);
        break;
    case K_GROUPKEY:
        p = fmt_str(p, v->b ? "STATE incident group PRESENT key(grp)=" : "STATE incident group REMOVED key(grp)=");
        p = fmt_u64(p, v->a); p = fmt_str(p, " (tracked via guest "); p = fmt_u64(p, v->c); *p++ = ')';
        break;
    case K_SOS:
        p = fmt_str(p, "ENTRY SOS-step guest="); p = fmt_u64(p, v->a);
        break;
    case K_RECOVER:
        p = fmt_str(p, "ENTRY recovery-receiver grp="); p = fmt_u64(p, v->a);
        p = fmt_str(p, " beh="); p = fmt_str(p, pd_beh_name(v->b));
        p = fmt_str(p, " +8="); p = fmt_u64(p, v->c); p = fmt_str(p, " +9="); p = fmt_u64(p, v->d);
        p = fmt_str(p, v->e ? " => predicted PROCEED" : " => predicted SKIP");
        break;
    case K_EXITPHYS:
        p = fmt_str(p, "ENTRY exit-behaviour(Physics) grp="); p = fmt_u64(p, v->a);
        p = fmt_str(p, " members="); p = fmt_u64(p, v->b); p = fmt_str(p, " +0x1b="); p = fmt_u64(p, v->c);
        p = fmt_str(p, " handle="); p = fmt_hex(p, v->d);
        break;
    case K_EXITMEMBER:
        p = fmt_str(p, "ENTRY exit-behaviour(Physics) grp="); p = fmt_u64(p, v->a);
        p = fmt_str(p, " member#"); p = fmt_u64(p, v->c); p = fmt_str(p, " guest="); p = fmt_u64(p, v->b);
        break;
    case K_PURGE:
        p = fmt_str(p, "SANITY purge (unloading) ids="); p = fmt_u64(p, v->a);
        if (v->a) { p = fmt_str(p, " first="); p = fmt_u64(p, v->b); }
        break;
    case K_UNTRACKED:
        p = fmt_str(p, "NOTE launch not tracked (table full) guest="); p = fmt_u64(p, v->a);
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
        if (s1 == 0 || s1 < i + 1) break;            /* still being written */
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

/* helper -> scripts: next id. 2 = none, 3 = group index, 4 = guest id; then 64 x irs_pd_bit (MSB first): 1 = set, 2 = clear */
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

/* scripts -> helper: values via begin / bit0 / bit1 / push, then irs_pd_note */
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
/* notes: [1, guest, scriptGroupId+1 (0 = none)] ; [2, grp, behaviour code+1 (0 = unreadable)] ; [3, nGuestsInvolved] ; [4, trapped count] */
__declspec(dllexport) int irs_pd_note(void *L)
{
    (void)L;
    if (g_pd_nvals < 1) return 2;
    uint64_t k = g_pd_vals[0], a = g_pd_nvals > 1 ? g_pd_vals[1] : 0, c = g_pd_nvals > 2 ? g_pd_vals[2] : 0;
    char b[200], *p = b;
    p = fmt_str(p, "pd t="); p = fmt_u64(p, GetTickCount64() - g_pd_t0); p = fmt_str(p, "ms SCRIPT ");
    switch (k) {
    case 1:
        p = fmt_str(p, "guest="); p = fmt_u64(p, a);
        if (c) { p = fmt_str(p, " GetGuestGroupID="); p = fmt_u64(p, c - 1); }
        else p = fmt_str(p, " GetGuestGroupID=(none/unreadable)");
        break;
    case 2:
        p = fmt_str(p, "STATE grp="); p = fmt_u64(p, a); p = fmt_str(p, " displayed behaviour now ");
        p = fmt_str(p, c ? pd_beh_name(c - 1) : "(unreadable)");
        break;
    case 3:
        p = fmt_str(p, "message GuestPhysicsIncidentEnded received, nGuestsInvolved="); p = fmt_u64(p, a);
        break;
    case 4:
        p = fmt_str(p, "trapped (SOS) guests now "); p = fmt_u64(p, a);
        break;
    default:
        p = fmt_str(p, "note "); p = fmt_u64(p, k);
    }
    *p = 0; log_line(b);
    return 1;
}
