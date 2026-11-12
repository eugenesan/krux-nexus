#!/usr/bin/env python3
"""Format the baked translation files the way the upstream tree keeps them.

``i18n/i18n.py bake`` writes the arrays as one long line (a plain
``repr``); the tree keeps them one entry per line, the way ``black``
leaves them.  Running this after a bake restores that layout, so the
diff of a baked file shows the strings that changed and nothing else.

Usage:
    python3 scripts/misc/format-baked-translations.py [krux_dir]
"""

import ast
import os
import sys


def black_string(text):
    """A string literal the way black writes it: double quotes preferred"""
    if not isinstance(text, str):
        # the reference array holds numbers; leave them alone
        return repr(text)
    literal = repr(text)
    if literal.startswith('"'):
        # it holds a single quote and no double quote: black keeps it
        return literal
    body = literal[1:-1]
    if '"' in body:
        # it holds a double quote: black keeps the single quotes
        return literal
    # switch to double quotes and let the apostrophes out
    return '"' + body.replace("\\'", "'") + '"'


def format_list(text, name):
    """Rewrite ``name = [...]`` as a one-entry-per-line list"""
    marker = name + " = "
    start = text.index(marker) + len(marker)
    end = text.index("[", start)
    depth = 0
    for i in range(end, len(text)):
        if text[i] == "[":
            depth += 1
        elif text[i] == "]":
            depth -= 1
            if depth == 0:
                last = i
                break
    items = ast.literal_eval(text[end : last + 1])
    body = "".join("    %s,\n" % black_string(item) for item in items)
    return text[:end] + "[\n" + body + "]\n" + text[last + 2 :]


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "krux"
    directory = os.path.join(root, "src", "krux", "translations")
    for name in sorted(os.listdir(directory)):
        if not name.endswith(".py"):
            continue
        path = os.path.join(directory, name)
        with open(path, encoding="utf-8") as f:
            text = f.read()
        original = text
        if "translation_array = " in text:
            text = format_list(text, "translation_array")
        if "available_languages = " in text:
            text = format_list(text, "available_languages")
        if "ref_array = " in text:
            text = format_list(text, "ref_array")
        if text != original:
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
            print("formatted " + path)


if __name__ == "__main__":
    main()
