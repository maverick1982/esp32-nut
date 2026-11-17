#!/usr/bin/env python3
"""Renumber the NUT line citations (<file>-hid.c:N, :N-M, :N, M) after nut_repo moves.

The line map between the two commits comes from the diff of each upstream file
(difflib on `git show <ref>:<path>`), never from hand-written offsets: a line that
is unchanged between the two refs gets its new number, a line that was changed or
removed is not mapped and its citation is left as it is and printed, to be fixed
by hand (ADR 0011 rule 1).

Usage:
  python -I scripts/nut_line_map.py --repo nut_repo --old <ref> --new <ref> \
      --upstream drivers/mge-hid.c drivers/apc-hid.c ... \
      --sources lib/USBHostUPS/src/NutTable.h ... [--dry-run]

Files are read and written as bytes and decoded as UTF-8: line endings are not
converted. After writing, a file that gained a NUL or a CR it did not have makes
the script exit with an error.
"""

import argparse
import difflib
import os
import re
import subprocess
import sys


def git_lines(repo, ref, path):
    out = subprocess.run(["git", "-C", repo, "show", f"{ref}:{path}"],
                         check=True, stdout=subprocess.PIPE).stdout
    return out.decode("utf-8", errors="surrogateescape").split("\n")


def line_map(old_lines, new_lines):
    """1-based map old -> new for the lines that the diff keeps equal."""
    m = {}
    sm = difflib.SequenceMatcher(None, old_lines, new_lines, autojunk=False)
    for tag, i1, i2, j1, _ in sm.get_opcodes():
        if tag == "equal":
            for k in range(i2 - i1):
                m[i1 + k + 1] = j1 + k + 1
    return m


# <basename>:N, :N-M, and comma lists that follow (":N, M, P-Q")
NUM = r"\d+(?:-\d+)?(?![\w])"  # not the 0 of "2160, 0xFFFF004A"


def citation_re(basename):
    return re.compile(re.escape(basename) + r":(" + NUM + r"(?:, ?" + NUM + r")*)")


def remap_item(item, m):
    """'N' or 'N-M' -> (new text, list of unmapped numbers)."""
    parts = item.split("-")
    out, missing = [], []
    for p in parts:
        n = int(p)
        if n in m:
            out.append(str(m[n]))
        else:
            out.append(p)
            missing.append(n)
    return "-".join(out), missing


def process(text, maps, path, report):
    lines = text.split("\n")
    for idx, line in enumerate(lines):
        for basename, (rx, m) in maps.items():
            def repl(mo):
                items = re.split(r"(, ?)", mo.group(1))
                new_items, missing_all = [], []
                for it in items:
                    if re.fullmatch(NUM, it):
                        new, missing = remap_item(it, m)
                        missing_all += missing
                        new_items.append(new)
                    else:
                        new_items.append(it)
                new = basename + ":" + "".join(new_items)
                old = mo.group(0)
                if missing_all:
                    report["unmapped"].append(
                        f"{path}:{idx + 1} -> {old} (unmapped: {', '.join(map(str, missing_all))})")
                if new != old:
                    report["changed"].append(f"{path}:{idx + 1}: {old} -> {new}")
                return new
            line = rx.sub(repl, line)
        lines[idx] = line
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--repo", default="nut_repo")
    ap.add_argument("--old", required=True)
    ap.add_argument("--new", required=True)
    ap.add_argument("--upstream", nargs="+", required=True, help="paths inside the repo, e.g. drivers/mge-hid.c")
    ap.add_argument("--sources", nargs="+", required=True)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    maps = {}
    for path in args.upstream:
        m = line_map(git_lines(args.repo, args.old, path), git_lines(args.repo, args.new, path))
        base = os.path.basename(path)
        maps[base] = (citation_re(base), m)

    report = {"changed": [], "unmapped": []}
    failed = False
    for src in args.sources:
        with open(src, "rb") as f:
            raw = f.read()
        text = raw.decode("utf-8", errors="surrogateescape")
        new_text = process(text, maps, src, report)
        if new_text == text or args.dry_run:
            continue
        out = new_text.encode("utf-8", errors="surrogateescape")
        with open(src, "wb") as f:
            f.write(out)
        for bad, name in ((b"\x00", "NUL"), (b"\r", "CR")):
            if bad in out and bad not in raw:
                print(f"ERROR: {src} gained a {name}", file=sys.stderr)
                failed = True

    for line in report["changed"]:
        print(line)
    if report["unmapped"]:
        print("\nCitations left unchanged (line changed or removed upstream):")
        for line in report["unmapped"]:
            print("  " + line)
    print(f"\n{len(report['changed'])} citations renumbered, {len(report['unmapped'])} left to fix by hand"
          + (" (dry run, nothing written)" if args.dry_run else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
