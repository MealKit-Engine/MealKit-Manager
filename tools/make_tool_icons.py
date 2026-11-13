"""Regenerate src/ToolIcons.h: the 128 px PNG of each suite tool's app icon, for the Tools page.

    python tools/make_tool_icons.py            -> reads <workspace>/<Tool>/tool/res/<Tool>.ico, writes src/ToolIcons.h
    python tools/make_tool_icons.py --check    -> exit 1 if src/ToolIcons.h differs from the .ico files

Run it after the engine's tools/make_suite_icons.py changes the tool icons. The workspace is the folder that holds
this repo and the tool repos side by side.
"""
import argparse
import os
import struct
import sys

TOOLS = ["MkAudio", "MkUI", "MkRender", "MkQA"]  # Tools page order (App.cpp)
SIZE = 128

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORKSPACE = os.path.dirname(HERE)
HEADER = os.path.join(HERE, "src", "ToolIcons.h")


def png_from_ico(path, size):
    data = open(path, "rb").read()
    count = struct.unpack_from("<H", data, 4)[0]
    for i in range(count):
        w, h, _, _, _, _, length, offset = struct.unpack_from("<BBBBHHII", data, 6 + 16 * i)
        if w == size and h == size:
            image = data[offset:offset + length]
            if not image.startswith(b"\x89PNG"):
                sys.exit(f"{path}: the {size} px image is not a PNG")
            return image
    sys.exit(f"{path}: no {size} px image")


def header():
    out = [
        "#pragma once",
        "",
        "// App icons of the suite's tools (128 px PNGs) for the Tools page. Copied from each tool repo's tool/res/<Tool>.ico",
        "// (the Manager doesn't read tool files at run time) by tools/make_tool_icons.py; run it when those files change.",
        "#include <cstddef>",
        "",
        "namespace mkmanager {",
        "",
    ]
    for tool in TOOLS:
        rel = f"{tool}/tool/res/{tool}.ico"
        image = png_from_ico(os.path.join(WORKSPACE, rel), SIZE)
        out.append(f"// {rel}")
        out.append(f"inline constexpr unsigned char kIcon{tool}[] = {{")
        for i in range(0, len(image), 24):
            out.append("    " + "".join(f"0x{b:02X}," for b in image[i:i + 24]))
        out.append("};")
        out.append("")
    out.append("} // namespace mkmanager")
    return "\n".join(out) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="only report whether src/ToolIcons.h is current")
    args = parser.parse_args()
    text = header()
    current = open(HEADER, encoding="utf-8", newline="").read() if os.path.exists(HEADER) else ""
    if args.check:
        print("ToolIcons.h is current" if text == current else "ToolIcons.h is out of date: run tools/make_tool_icons.py")
        sys.exit(0 if text == current else 1)
    if text == current:
        print("unchanged", HEADER)
        return
    with open(HEADER, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote", HEADER)


if __name__ == "__main__":
    main()
