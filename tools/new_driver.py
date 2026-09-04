#!/usr/bin/env python3
"""Creates a new driver from the templates in docs/templates.

    python3 tools/new_driver.py galaxian --class Galaxian --title "Galaxian"
"""

import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TEMPLATES = ROOT / "docs" / "templates"
DRIVERS = ROOT / "src" / "drivers"
KINDS = ("arcade", "computers", "consoles")


def render(template: pathlib.Path, replacements: dict) -> str:
    text = template.read_text()
    for key, value in replacements.items():
        text = text.replace("{{%s}}" % key, value)
    return text


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("name", help="driver file name, e.g. galaxian")
    parser.add_argument("--class", dest="class_name", help="C++ class name (default: Name)")
    parser.add_argument("--title", help="window title (default: the class name)")
    parser.add_argument("--pascal", help="Pascal unit it is ported from")
    parser.add_argument("--force", action="store_true", help="overwrite existing files")
    parser.add_argument("--kind", choices=KINDS, default="arcade",
                        help="src/drivers subfolder (default: arcade)")
    args = parser.parse_args()

    name = args.name.lower()
    class_name = args.class_name or name.capitalize()
    kind = args.kind
    replacements = {
        "FILE": name,
        "CLASS": class_name,
        "TITLE": args.title or class_name,
        "PASCAL": args.pascal or "%s_hw.pas" % name,
        "KIND": kind,
    }

    written = []
    out_dir = DRIVERS / kind
    out_dir.mkdir(parents=True, exist_ok=True)
    for suffix in ("h", "cpp"):
        target = out_dir / f"{name}.{suffix}"
        if target.exists() and not args.force:
            print(f"{target} already exists, use --force to overwrite", file=sys.stderr)
            return 1
        target.write_text(render(TEMPLATES / f"driver.{suffix}.template", replacements))
        written.append(target)

    for target in written:
        print(f"wrote {target.relative_to(ROOT)}")
    print("\nAdd it to CMakeLists.txt (both targets that need it):")
    print(f"  src/drivers/{kind}/{name}.cpp")
    print("\nAnd to src/main.cpp:")
    print(f'  #include "drivers/{kind}/{name}.h"')
    print(f'  if (game == "{name}") return std::make_unique<dsp::{class_name}>();')
    print("\nThen follow docs/adding-a-driver.md.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
