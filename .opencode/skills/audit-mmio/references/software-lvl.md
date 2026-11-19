# The SOFTWARE_LVL case study

## What happened

`GLOBALSEC` stores its region-control registers in one array:

```c
#define REG_GLOBALSEC_REGION_COUNT 0xB8
static uint32_t region_regs[REG_GLOBALSEC_REGION_COUNT];
```

`0xB8 * 4 = 0x2E0`. The last word in that window was `0x2DC`, which is
`SOFTWARE_LVL`. The permission runlevel registers at `0x2D0`, `0x2D4`, `0x2D8`,
and `0x2DC` were treated as plain storage with a default of `0`, because
`region_index()` accepted every aligned word below `0x2E0`.

No fault, no warning, no log. The RO image wrote `SOFTWARE_LVL` and read back
`0x3C`/`0x33` only because it wrote those exact values; the hardware behavior
(default `0x55`, decrement on every write, clamp at `0x00`) was gone.

## Why the boot still passed

`component_log_unhandled` was never called. The generic array answered the
access, so `runtime_fault` never fired. The boot smoke test only proved that
the image reached the next unhandled register, not that GLOBALSEC behaved.

## The fix

- `REG_GLOBALSEC_REGION_COUNT` is `0xB4`, so the region array ends at `0x2CC`.
- `permission_index()` runs before `region_index()` and owns `0x2D0..0x2DC`.
- A `_Static_assert` in `globalsec.h` fails the build if the region count grows
  back over the permission registers.

## The rules it produced

1. A range count must end exactly at the next register, with a static assert.
2. A special register is dispatched before its surrounding range.
3. Unknown offsets fault; a catch-all must be an explicit, documented exception.
4. A boot that reaches a later register says nothing about the registers it
   already touched.

## Reproduction

The checker flags the original code:

```text
audit-mmio: FAIL GLOBALSEC: REG_GLOBALSEC_REGION_COUNT covers 0x2d0
  (CPU0_S_PERMISSION), which is a different register family
```

Run it with the real definitions:

```bash
.opencode/skills/audit-mmio/scripts/audit-mmio.py \
  --regdefs /path/to/cr50/chip/haven/hw_b1_regdefs.h
```

A runlevel readback blob should show:

```text
SOFTWARE_LVL -> 0x55, 0x3c, 0x3c, 0x33, 0x33
```

That is read, write, read, write, read.
