#include "includes/htmodloader.h"
#include "aliases.h"
#include "outof0.h"

// ----------------------------------------------------------------------------
// Sky's built-in frame limiter (0.35.2).
//
// The limiter keeps its requested frame rate in a field of its own object and
// derives the effective rate from the display refresh rate:
//   - below 200 Hz it uses the display rate directly,
//   - 200..299 Hz it halves it,
//   - 300 Hz and above it divides it by three (capped at 400/3).
// That means a 240 Hz panel is limited to 120 FPS no matter what quality
// setting is used.
//
// We make two changes:
//   1. Patch the short branch that selects the display-derived rate so the
//      engine always uses the requested target rate instead.
//   2. Hook the limiter to temporarily replace the requested rate with the
//      value selected in the mod UI.
// ----------------------------------------------------------------------------

// Object field used by the limiter in Sky 0.35.2.
#define OZ_OFF_LIMITER_TARGET_FPS 0x4AC

typedef void (__fastcall *PFN_FrameLimiter)(
  void *self);

static const HTAsmSig sig_FrameLimiter = {
  .sig =
    "55 56 48 83 EC 58 48 8D 6C 24 50 C5 F9 29 75 F0 48 89 CE "
    "48 8B 49 40 48 8D 55 E8 4C 8D 45 EC E8 ? ? ? ? 84 C0 74 52 "
    "8B 86 40 05 00 00",
  .indirect = HT_SCAN_DIRECT,
  .offset = 0
};

static HTAsmFunction sfn_FrameLimiter = {
  .name = "Sky::FrameLimiter",
  .detour = NULL,
  .fn = NULL,
  .origin = NULL
};

static void __fastcall hook_FrameLimiter(
  void *self
) {
  i32 oldTarget = 0;
  i32 target;

  if (self) {
    oldTarget = *(i32 *)((u08 *)self + OZ_OFF_LIMITER_TARGET_FPS);

    target = gFpsLimit;
    if (target >= 1000)
      // "No Limit" in the UI.
      target = 100000;
    if (target < 15)
      target = 15;

    *(i32 *)((u08 *)self + OZ_OFF_LIMITER_TARGET_FPS) = target;
  }

  ((PFN_FrameLimiter)sfn_FrameLimiter.origin)(self);

  if (self)
    *(i32 *)((u08 *)self + OZ_OFF_LIMITER_TARGET_FPS) = oldTarget;
}

// Replace the display-rate selection branch with an unconditional branch to
// the "use requested target rate" path.
static u08 OzPatchLimiterBranch(
  void *fn
) {
  u08 *code = (u08 *)fn;
  DWORD oldProtect, tmpProtect;
  i32 i;

  for (i = 0; i < 0x200; i++) {
    i08 rel;
    u08 *target;

    if (code[i] != 0x77)
      // JA rel8
      continue;

    rel = (i08)code[i + 1];
    target = code + i + 2 + rel;
    if (
      target <= code + i + 2
      || target >= code + 0x300)
      continue;

    // The target path starts with VCVTSI2SD.
    if (target[0] != 0xC5 || target[2] != 0x2A)
      continue;

    if (!VirtualProtect(code + i, 2, PAGE_EXECUTE_READWRITE, &oldProtect))
      return 0;

    // JA -> JMP, keeping the original relative offset.
    code[i] = 0xEB;
    VirtualProtect(code + i, 2, oldProtect, &tmpProtect);
    FlushInstructionCache(GetCurrentProcess(), code + i, 2);
    return 1;
  }

  return 0;
}

void OzInstallAllHooks() {
  if (!HTSigScanFunc(
    &sig_FrameLimiter,
    &sfn_FrameLimiter)
    || !sfn_FrameLimiter.fn) {
    HTTellRaw(
      "[Out of 0] Unsupported Sky build: frame limiter not found.\n");
    return;
  }

  sfn_FrameLimiter.detour = (void *)hook_FrameLimiter;
  if (HTAsmHookCreate(
    hModuleDll,
    &sfn_FrameLimiter) != HT_SUCCESS) {
    HTTellRaw(
      "[Out of 0] Failed to hook the frame limiter.\n");
    return;
  }

  if (!OzPatchLimiterBranch(sfn_FrameLimiter.fn)) {
    HTTellRaw("[Out of 0] Failed to patch the frame limiter.\n");
    return;
  }

  HTAsmHookEnable(
    hModuleDll,
    sfn_FrameLimiter.fn);
}
