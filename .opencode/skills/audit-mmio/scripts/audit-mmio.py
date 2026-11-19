#!/usr/bin/env python3
"""Audit H1B3C MMIO register ranges against the Cr50 hw_b1 register defs."""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

COMPONENTS = [
    "GLOBALSEC",
    "PINMUX",
    "PMU",
    "KEYMGR",
    "FLASH",
    "UART",
    "TRNG",
    "FUSE",
    "M3",
    "GPIO",
    "USB",
    "RTC",
    "TIMELS",
    "WATCHDOG",
    "RBOX",
    "CRYPTO",
    "XO",
    "SWDP",
]

# component, base macro, count macro, name predicate.
# A range may only cover registers that belong to that one register family.
RANGES = [
    ("GLOBALSEC", "REG_GLOBALSEC_REGION_OFFSET", "REG_GLOBALSEC_REGION_COUNT",
     lambda name: "REGION" in name or "BULKERASE" in name),
    ("GLOBALSEC", "REG_GLOBALSEC_SB_BL_SIG0_OFFSET", "REG_GLOBALSEC_SB_BL_SIG_COUNT",
     lambda name: re.fullmatch(r"SB_BL_SIG\d+", name) is not None),
    ("PMU", "REG_PMU_PWRDN_SCRATCH_OFFSET", "REG_PMU_PWRDN_SCRATCH_COUNT",
     lambda name: re.fullmatch(r"PWRDN_SCRATCH\d+", name) is not None),
    ("PMU", "REG_PMU_PWRDN_SCRATCH_LOCK_OFFSET", "REG_PMU_PWRDN_SCRATCH_LOCK_COUNT",
     lambda name: re.fullmatch(r"PWRDN_SCRATCH_LOCK\d*", name) is not None),
    ("PMU", "REG_PMU_LONG_LIFE_SCRATCH_OFFSET", "REG_PMU_LONG_LIFE_SCRATCH_COUNT",
     lambda name: re.fullmatch(r"LONG_LIFE_SCRATCH\d+", name) is not None),
    ("KEYMGR", "REG_KEYMGR_HKEY_RWR0_OFFSET", "REG_KEYMGR_HKEY_RWR_COUNT",
     lambda name: re.fullmatch(r"HKEY_RWR\d+", name) is not None),
    ("KEYMGR", "REG_KEYMGR_HKEY_FWR0_OFFSET", "REG_KEYMGR_HKEY_FWR_COUNT",
     lambda name: re.fullmatch(r"HKEY_FWR\d+", name) is not None),
    ("KEYMGR", "REG_KEYMGR_HKEY_FRR0_OFFSET", "REG_KEYMGR_HKEY_FRR_COUNT",
     lambda name: re.fullmatch(r"HKEY_FRR\d+", name) is not None),
    ("KEYMGR", "REG_KEYMGR_SHA_STS_H0_OFFSET", "REG_KEYMGR_SHA_STS_H_COUNT",
     lambda name: re.fullmatch(r"SHA_STS_H\d+", name) is not None),
    ("KEYMGR", "REG_KEYMGR_SHA_KEY_W0_OFFSET", "REG_KEYMGR_SHA_KEY_W_COUNT",
     lambda name: re.fullmatch(r"SHA_KEY_W\d+", name) is not None),
    ("FLASH", "REG_FLASH_WR_DATA0_OFFSET", "REG_FLASH_WR_DATA_COUNT",
     lambda name: re.fullmatch(r"FSH_WR_DATA\d+", name) is not None),
    ("GPIO", "REG_GPIO_MASKLOWBYTE_OFFSET", "REG_GPIO_MASK_COUNT",
     lambda name: name.startswith("MASKLOWBYTE_")),
    ("GPIO", "REG_GPIO_MASKHIGHBYTE_OFFSET", "REG_GPIO_MASK_COUNT",
     lambda name: name.startswith("MASKHIGHBYTE_")),
    ("RBOX", "REG_RBOX_OFFSET", "REG_RBOX_COUNT", lambda name: True),
    ("CRYPTO", "REG_CRYPTO_OFFSET", "REG_CRYPTO_COUNT", lambda name: True),
    ("CRYPTO", "REG_CRYPTO_DMEM_OFFSET", "REG_CRYPTO_DMEM_COUNT", lambda name: True),
    ("CRYPTO", "REG_CRYPTO_IMEM_OFFSET", "REG_CRYPTO_IMEM_COUNT", lambda name: True),
    ("XO", "REG_XO_OFFSET", "REG_XO_COUNT", lambda name: True),
    ("PINMUX", None, "REG_PINMUX_REG_COUNT", lambda name: True),
]

DEFINE_RE = re.compile(r"#define\s+([A-Z0-9_]+)\s+(0x[0-9a-fA-F]+|\d+)")
OFFSET_RE = re.compile(r"^REG_([A-Z0-9]+)_([A-Z0-9_]+)_OFFSET$")

KNOWN_GAPS = []

RANGE_SKIP_COVERAGE = {
    "REG_M3_NVIC_IPR_COUNT": "ARMv7-M has 64 IPR words; hw_b1 only lists the first 8",
    "REG_CRYPTO_DMEM_COUNT": "hw_b1 lists only the DMEM base; accelerator memory is 1024 words",
    "REG_CRYPTO_IMEM_COUNT": "hw_b1 lists only the IMEM base; accelerator memory is 1024 words",
}

HEADER_SKIP = {
    "REG_M3_NVIC_STIR_OFFSET": "ARMv7-M NVIC STIR; hw_b1 regdefs omit it",
}


def repo_root() -> Path:
    try:
        out = subprocess.check_output(["git", "rev-parse", "--show-toplevel"],
                                      stderr=subprocess.DEVNULL, text=True)
        return Path(out.strip())
    except (subprocess.CalledProcessError, FileNotFoundError):
        return Path(__file__).resolve().parents[4]


def find_regdefs(root: Path, explicit: str | None) -> Path | None:
    candidates = []
    if explicit:
        candidates.append(Path(explicit))
    env = os.environ.get("CR50_HW_B1_REGDEFS")
    if env:
        candidates.append(Path(env))
    candidates.extend([
        root / "reference/cr50/chip/haven/hw_b1_regdefs.h",
        root.parent / "cr50/chip/haven/hw_b1_regdefs.h",
        Path("/git/HavenOverflow/cr50/chip/haven/hw_b1_regdefs.h"),
    ])
    for path in candidates:
        if path.is_file():
            return path
    return None


def component_for_macro(macro: str) -> str | None:
    best = None
    for component in COMPONENTS:
        if macro == component or macro.startswith(component + "_"):
            if best is None or len(component) > len(best):
                best = component
    return best


def parse_regdefs(path: Path) -> dict[str, dict[int, list[str]]]:
    by_component: dict[str, dict[int, list[str]]] = {}
    for macro, value in DEFINE_RE.findall(path.read_text()):
        if not macro.startswith("GC_") or not macro.endswith("_OFFSET"):
            continue
        name = macro[3:-len("_OFFSET")]
        component = component_for_macro(name)
        if component is None:
            continue
        if not (name == component or name.startswith(component + "_")):
            continue
        short = name[len(component) + 1:] if name != component else ""
        by_component.setdefault(component, {}).setdefault(
            int(value, 0), []).append(short)
    return by_component


def parse_headers(include_dir: Path) -> tuple[dict[str, int], dict[str, str]]:
    values: dict[str, int] = {}
    files: dict[str, str] = {}
    for path in sorted(include_dir.glob("*.h")):
        component = path.stem.upper()
        if component == "FUSES":
            component = "FUSE"
        text = path.read_text()
        files[component] = text
        for macro, value in DEFINE_RE.findall(text):
            values[macro] = int(value, 0)
    return values, files


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--regdefs", help="path to chip/haven/hw_b1_regdefs.h")
    parser.add_argument("--root", help="repo root")
    args = parser.parse_args()

    root = Path(args.root) if args.root else repo_root()
    regdefs = find_regdefs(root, args.regdefs)
    if regdefs is None:
        print("audit-mmio: cannot find hw_b1_regdefs.h; pass --regdefs PATH",
              file=sys.stderr)
        return 2

    hw = parse_regdefs(regdefs)
    include_dir = root / "include/H1B3C/peripherals"
    values, headers = parse_headers(include_dir)
    errors: list[str] = []

    sizes = {"PMU": 0x20000}
    default_size = 0x10000
    mapping_re = re.compile(
        r"\.component = (\w+),\s*\.base = (REG_[A-Z0-9_]+_BASE_ADDR),\s*"
        r"\.size = ([A-Z0-9_]+)")
    init_text = (root / "src/H1B3C/peripherals/_init.c").read_text()
    mapped = set()
    for var, base_macro, size_macro in mapping_re.findall(init_text):
        if var == "eFuses":
            component = "FUSE"
        elif var in ("GPIO0", "GPIO1"):
            component = "GPIO"
        elif var in ("UART0", "UART1", "UART2"):
            component = "UART"
        else:
            component = var.upper()
        mapped.add(component)
        if base_macro not in values:
            errors.append(f"{component}: unknown base macro {base_macro}")
        if size_macro == "DEFAULT_REG_LENGTH":
            size = default_size
        elif size_macro in values:
            size = values[size_macro]
        else:
            errors.append(f"{component}: unknown size macro {size_macro}")
            continue
        expected = sizes.get(component, default_size)
        if size != expected:
            errors.append(
                f"{component}: MMIO size 0x{size:x}, expected 0x{expected:x}")
    for component in sorted(hw):
        if component not in mapped:
            errors.append(f"{component}: hw_b1 component is not mapped in _init.c")

    for component, text in headers.items():
        if component not in hw:
            continue
        for macro, value in DEFINE_RE.findall(text):
            match = OFFSET_RE.match(macro)
            if not match or match.group(1) != component:
                continue
            if match.group(2) == "BASE_ADDR":
                continue
            if macro in HEADER_SKIP:
                print(f"audit-mmio: note: skipped {macro}: {HEADER_SKIP[macro]}")
                continue
            if int(value, 0) not in hw[component]:
                errors.append(
                    f"{component}: {macro}=0x{int(value, 0):x} "
                    "is not a hw_b1 register offset")

    covered: dict[str, list[tuple[int, int, str]]] = {}
    for component, base_macro, count_macro, predicate in RANGES:
        if component not in headers:
            errors.append(f"{component}: missing component header")
            continue
        if count_macro not in values:
            errors.append(f"{component}: missing range macro {count_macro}")
            continue
        base = values[base_macro] if base_macro else 0
        count = values[count_macro]
        end = base + count * 4
        covered.setdefault(component, [])
        for other_base, other_end, other_name in covered[component]:
            if base < other_end and other_base < end:
                errors.append(
                    f"{component}: {count_macro} range 0x{base:x}..0x{end - 1:x} "
                    f"overlaps {other_name} 0x{other_base:x}..0x{other_end - 1:x}")
        covered[component].append((base, end, count_macro))

        if count_macro in RANGE_SKIP_COVERAGE:
            print(f"audit-mmio: note: skipped coverage for {count_macro}: "
                  f"{RANGE_SKIP_COVERAGE[count_macro]}")
            continue

        for offset in range(base, end, 4):
            names = hw[component].get(offset, [])
            if not names:
                errors.append(
                    f"{component}: {count_macro} covers 0x{offset:x}, "
                    "which is not a hw_b1 register")
                continue
            if not any(predicate(name) for name in names):
                errors.append(
                    f"{component}: {count_macro} covers 0x{offset:x} "
                    f"({', '.join(names)}), which is a different register family")

    for path in sorted((root / "src/H1B3C/peripherals").glob("*.c")):
        if path.name in ("fuses.c", "_init.c"):
            continue
        if "component_log_unhandled" not in path.read_text():
            errors.append(
                f"{path.name}: no component_log_unhandled fallback; "
                "unknown offsets must fault")

    print(f"audit-mmio: {len(hw)} components, {len(values)} register macros, "
          f"{sum(len(v) for v in covered.values())} ranges")
    for gap in KNOWN_GAPS:
        print(f"audit-mmio: note: {gap}")

    for error in errors:
        print(f"audit-mmio: FAIL {error}", file=sys.stderr)
    if errors:
        return 1

    print("audit-mmio: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
