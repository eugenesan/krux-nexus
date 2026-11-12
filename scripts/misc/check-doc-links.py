#!/usr/bin/env python3
"""Check the markdown links in the repository.

Every ``[text](target)`` link in the tracked markdown is resolved against
the file it appears in.  Links to a missing file are reported; links that
point outside the repository (http, mailto, anchors) are ignored.

Usage:
    python3 scripts/misc/check-doc-links.py [root]

Exit status is 1 when a link is broken, so it can be used in a check.
"""

import os
import re
import sys

LINK = re.compile(r"\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
SKIP_DIRS = {".git", "linux", "vendor", "artifacts", "__pycache__", ".venv"}
SKIP_SUFFIX = (".pyc",)


def markdown_files(root):
    """Every markdown file that is not in a skipped directory"""
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for name in files:
            if name.endswith(".md") and not name.endswith(SKIP_SUFFIX):
                yield os.path.join(base, name)


def check(root):
    """Report the broken links; returns the list of them"""
    broken = []
    for path in markdown_files(root):
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        for lineno, line in enumerate(text.splitlines(), 1):
            for match in LINK.finditer(line):
                target = match.group(1).strip()
                if (
                    not target
                    or target.startswith(("#", "http://", "https://", "mailto:"))
                ):
                    continue
                target = target.split("#")[0]
                if not target:
                    continue
                resolved = os.path.normpath(
                    os.path.join(os.path.dirname(path), target)
                )
                if not os.path.exists(resolved):
                    broken.append((path, lineno, target))
    return broken


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    broken = check(root)
    for path, lineno, target in broken:
        print(f"{path}:{lineno}: broken link -> {target}")
    print(f"{len(broken)} broken link(s)")
    return 1 if broken else 0


if __name__ == "__main__":
    sys.exit(main())
