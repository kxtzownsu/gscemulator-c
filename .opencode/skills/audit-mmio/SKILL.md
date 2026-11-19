---
name: Audit MMIO registers
description: Audit better-gscemu H1B3C MMIO components against Cr50 hw_b1_regdefs.h and upstream GSCEmulator; use when adding, changing, or reviewing a peripheral register, array range, or read/write handler.
---

# Audit MMIO registers

Run the bundled checker before touching a peripheral:

```bash
.opencode/skills/audit-mmio/scripts/audit-mmio.py \
  --regdefs /path/to/cr50/chip/haven/hw_b1_regdefs.h
```

It fails when:

- a header offset is not a real `hw_b1` register offset
- a range count covers a register from another family
- two ranges overlap
- a component MMIO size does not match the hardware size
- a component has no `component_log_unhandled` fallback

## Rules

- Every handled offset is a named `REG_<COMPONENT>_<REGISTER>_OFFSET` macro. No
  numeric MMIO offsets in handlers.
- A range handler is only for a repeated register file. Its `*_COUNT` must end
  exactly at the next register. Add a `_Static_assert` in the header proving it.
- Never widen a range to include an adjacent register. Shrink or split it.
- A register with unique behavior inside a range is dispatched before the
  range handler: runlevel, lock, clear-on-write, trigger, constant, read-only.
- Unknown offsets call `component_log_unhandled`. The only catch-all is FUSE
  blank reads/writes, because upstream does the same.
- Boot success is not register coverage. The ROM only touches the paths it
  takes; a silent catch-all can fake success.
- Compare every changed register with the upstream handler and with the
  hardware field layout, not just the offset.

## Workflow

1. Run the checker.
2. Read the matching upstream component and `regdefs/registers.py` entry.
3. Add a readback blob that proves the default, both directions, and any
   lock/clear/decrement behavior.
4. Run a warning-free `make` and the boot smoke test.
5. Update `src/H1B3C/AGENTS.md` when the component behavior changes.

See [the SOFTWARE_LVL case study](references/software-lvl.md) for why a boot
that reaches `jump @00040400` is not proof that a register exists.
