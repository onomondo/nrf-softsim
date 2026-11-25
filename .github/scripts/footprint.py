#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Firmware footprint delta report (Markdown) from the GNU ld maps <base>/<image>.map and <pr>/<image>.map.

Region use matches `ld --print-memory-usage`; attribution comes from the map's input sections and adds up to it.
"""

import re
import sys
from collections import defaultdict

IMAGES = {"app": "app (NS)", "tfm": "TF-M (S)"}
REGIONS = ("FLASH", "RAM")
TOP = 12
WARN_PCT = 90.0
PADDING = "(padding and gaps)"
LITERALS = "(string literals, merged)"

ADDR = re.compile(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)(?:\s+load address 0x([0-9a-f]+))?(?:\s+(\S.*?))?\s*$")
SYM = re.compile(r"^\s+0x[0-9a-f]+\s+([A-Za-z_$][\w.$]*)$")
REGION = re.compile(r"^(\w+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)")
NON_ALLOC = re.compile(r"^\.(debug|comment|ARM\.attributes|gnu\.attributes|stab)|^\.?tbss$")
MERGE = re.compile(r"\.(str\d+\.\d+|cst\d+)$")
PREFIX = re.compile(r"^\.(text|rodata|data|bss|noinit|ramfunc)\.")


def parse_map(path):
    """Return regions {name: (origin, length)}, output sections and input sections."""
    with open(path, errors="replace") as f:
        lines = f.read().splitlines()
    try:
        start, end = lines.index("Memory Configuration"), lines.index("Linker script and memory map")
    except ValueError:
        sys.exit(f"{path}: not a GNU ld map file")
    regions = {m[1]: (int(m[2], 16), int(m[3], 16)) for m in map(REGION.match, lines[start:end]) if m}

    outputs, inputs = [], []  # (name, vma, size, lma) / [output, name, size, file, first symbol]
    cur = pending = None  # pending: (is_output, name) whose address/size wrapped to the next line

    def add(is_output, name, m):
        if is_output:
            outputs.append((name, int(m[1], 16), int(m[2], 16), int(m[3], 16) if m[3] else None))
        elif cur:
            inputs.append([cur, name, int(m[2], 16), m[4], None])

    for line in lines[end + 1:]:
        indent = len(line) - len(line.lstrip())
        if pending:
            (is_output, name), pending = pending, None
            if m := ADDR.match(line):
                add(is_output, name, m)
                continue
        if indent > 1:  # a symbol defined in the input section above: names Zephyr's .noinit buffers
            if (m := SYM.match(line)) and inputs and inputs[-1][0] == cur and inputs[-1][4] is None:
                inputs[-1][4] = m[1]
            continue
        tok = line.split(None, 1)
        if not tok or tok[0].startswith(("*(", "[")):
            continue
        if indent == 0:
            cur = tok[0]
        if len(tok) == 1:
            pending = (indent == 0, tok[0])
        elif m := ADDR.match(" " + tok[1]):
            add(indent == 0, tok[0], m)
    return regions, outputs, inputs


def component(file):
    """Group an object file, as the map spells it, into a component."""
    archive = file.split("(")[0] if file.endswith(")") else None
    parts = (archive or file).split("/")
    if "-zephyr-eabi" in file or parts[-1] in ("libc.a", "libm.a", "libgcc.a"):
        return "toolchain (libc, libgcc)"
    if "nrfxlib" in parts:
        return [p for p in parts[parts.index("nrfxlib") + 1:] if p != "crypto"][0] + " (nrfxlib)"
    if "onomondo-uicc" in parts:
        return "SoftSIM: onomondo-uicc"
    if parts[:2] == ["modules", "softsim"]:
        return "SoftSIM: nRF port"
    if parts[0] == "app":
        return "sample application"
    if parts[0] == "zephyr":
        return f"Zephyr {parts[1]}" if parts[1] in ("kernel", "arch", "drivers", "subsys", "lib", "soc") \
            and len(parts) > 2 else "Zephyr (generated, other)"
    if parts[0] == "modules" and len(parts) > 2:
        if parts[1] != "nrf":
            return parts[1]
        return f"NCS {parts[3]}" if len(parts) > 4 and parts[2] in ("lib", "subsys", "drivers", "modules") else "NCS"
    # TF-M and other non-Zephyr builds: one component per library.
    return re.sub(r"^lib|\.a$", "", parts[-1]) if archive else "(objects linked directly)"


def item_name(section, sym, file):
    """The function or object an input section holds, with its source file."""
    obj = re.sub(r"\.obj$", "", re.search(r"([^/()]+)\)?$", file)[1])
    if section.startswith(".noinit.") and "/" in section:  # .noinit.<path>.c.<n>: K_*_DEFINE buffers
        label = sym or section.rsplit(".", 1)[-1]
    else:
        m = PREFIX.match(section)
        label = section[m.end():] if m else sym or f"({section})"
    return f"{label}  ({obj})"


class Image:
    def __init__(self, path):
        self.regions, outputs, inputs = parse_map(path)
        self.used, self.comp, self.items = defaultdict(int), defaultdict(int), defaultdict(int)

        # As ld counts it: the highest address an output section reaches in the region (by VMA, and by
        # LMA for load copies such as .data), minus the region origin.
        place = {}
        for name, vma, size, lma in outputs:
            if not size or NON_ALLOC.search(name):
                continue
            place[name] = set()
            for addr in {vma, vma if lma is None else lma}:
                r = next((r for r, (o, n) in self.regions.items() if o <= addr < o + n), None)
                if r in REGIONS:
                    place[name].add(r)
                    self.used[r] = max(self.used[r], addr + size - self.regions[r][0])

        merged = set()
        for out, name, size, file, sym in inputs:
            if not size or out not in place:
                continue
            if file is None:  # *fill*
                comp = label = PADDING
            elif m := MERGE.search(name):
                # ld lists a merge group's first input with the whole merged blob, the rest at their
                # pre-merge size though they take no space; whose literal survived is not recorded.
                if (out, m[1]) in merged:
                    continue
                merged.add((out, m[1]))
                comp = label = LITERALS
            else:
                comp, label = component(file), item_name(name, sym, file)
            for r in place[out]:
                self.comp[(r, comp)] += size
                self.items[(r, label)] += size

        for r in REGIONS:
            gap = self.used[r] - sum(v for (rr, _), v in self.items.items() if rr == r)
            if gap < 0 or not self.used[r]:
                sys.exit(f"{path}: {r}: input sections do not add up to the {self.used[r]} B used")
            self.comp[(r, PADDING)] += gap
            self.items[(r, PADDING)] += gap


def kib(n):
    return f"{n / 1024:.1f} KiB"


def sign(n, bold=False):
    s = f"{n:+,d} B".replace(",", " ") if n else "0 B"
    return f"**{s}**" if bold and n else s


def diff(a, b):
    return {k: b[k] - a[k] for k in set(a) | set(b) if b[k] != a[k]}


def report(title, base, pr):
    """Headline rows, and the collapsed detail if anything changed."""
    rows = []
    for r in REGIONS:
        bu, pu, length = base.used[r], pr.used[r], pr.regions[r][1]
        pct = 100.0 * pu / length
        rows.append(f"| {title} | {r} | {kib(bu)} | {kib(pu)} | {sign(pu - bu, True)} "
                    f"| {pct:.1f}% of {kib(length)}{' ⚠️' if pct >= WARN_PCT else ''} |")
    comp, items = diff(base.comp, pr.comp), diff(base.items, pr.items)
    if not items:
        return rows, []

    # Changed components, largest change first; then the largest changed items per region with the
    # rest summed, so each region's list adds up to its headline delta.
    names = sorted({c for _, c in comp}, key=lambda c: (-max(abs(comp.get((r, c), 0)) for r in REGIONS), c))
    out = ["<details>", f"<summary>{title}: what changed</summary>\n",
           "| Component | " + " | ".join(f"{r} | Δ" for r in REGIONS) + " |", "|---|" + "---:|---:|" * len(REGIONS)]
    for c in names:
        out.append(f"| {c} | " + " | ".join(f"{kib(pr.comp[(r, c)])} | {sign(comp[(r, c)], True)}" if (r, c) in comp
                                            else " | " for r in REGIONS) + " |")
    out += ["", "```"]
    for r in REGIONS:
        ch = sorted(((d, k[1]) for k, d in items.items() if k[0] == r), key=lambda x: (-abs(x[0]), x[1]))
        if ch:
            out.append(f"{r} ({sign(pr.used[r] - base.used[r])}):")
            out += [f"  {d:+8d} B  {label}" for d, label in ch[:TOP]]
            if ch[TOP:]:
                out.append(f"  {sum(d for d, _ in ch[TOP:]):+8d} B  ({len(ch) - TOP} smaller changes)")
    return rows, out + ["```", "\n</details>\n"]


def main():
    if len(sys.argv) != 3:
        sys.exit(f"usage: {sys.argv[0]} BASE_DIR PR_DIR")
    rows, details = [], []
    for stem, title in IMAGES.items():
        r, d = report(title, *(Image(f"{dir}/{stem}.map") for dir in sys.argv[1:]))
        rows += r
        details += d
    print("\n".join(["| Image | Region | base | PR | Δ | PR use of region |", "|---|---|---:|---:|---:|---:|",
                     *rows, "", *(details or ["No change in any image."])]))


if __name__ == "__main__":
    main()
