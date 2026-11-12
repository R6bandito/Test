"""从 AAProj.uvprojx 生成仓库根目录的 compile_commands.json，供 Cursor / clangd 使用。

Keil 实际编译器是 ARMCC。这里的命令只给编辑器解析，目标三元组按 STM32F407 的 Cortex-M4F 填写。
"""

import json
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
UVPROJX = Path(__file__).resolve().parent / "AAProj.uvprojx"
OUT = ROOT / "compile_commands.json"

CLANG = "D:/llvm/bin/clang.exe"
SYS_INC = [
    "D:/llvm/lib/clang/18/include",
    (ROOT / ".cursor" / "clangd-libc" / "include").as_posix(),
]
ARMCC_STUB = (ROOT / ".cursor" / "clangd-libc" / "include" / "armcc_intrinsics.h").as_posix()
FLAGS = [
    "-std=gnu99",
    "--target=arm-none-eabi",
    "-mcpu=cortex-m4",
    "-mthumb",
    "-mfpu=fpv4-sp-d16",
    "-mfloat-abi=hard",
    "-ffreestanding",
    "-DUSE_HAL_DRIVER",
    "-DSTM32F407xx",
    "-include",
    ARMCC_STUB,
    "-Wno-unknown-attributes",
    "-Wno-unused-command-line-argument",
    "-Wno-implicit-function-declaration",
    "-ferror-limit=200",
]


def main() -> None:
    tree = ET.parse(UVPROJX)
    includes = []
    for node in tree.findall(".//Cads/VariousControls/IncludePath"):
        if node.text:
            for part in node.text.split(";"):
                part = part.strip()
                if part:
                    includes.append((UVPROJX.parent / part).resolve())

    files = []
    for node in tree.findall(".//File/FilePath"):
        if not node.text or not node.text.lower().endswith(".c"):
            continue
        files.append((UVPROJX.parent / node.text).resolve())

    entries = []
    for src in files:
        args = [CLANG, "-c", str(src).replace("\\", "/")]
        args.extend(FLAGS)
        for inc in SYS_INC:
            args.append("-isystem" + inc)
        for inc in includes:
            args.append("-I" + str(inc).replace("\\", "/"))
        entries.append(
            {
                "directory": str(ROOT).replace("\\", "/"),
                "file": str(src).replace("\\", "/"),
                "arguments": args,
            }
        )

    OUT.write_text(json.dumps(entries, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {len(entries)} entries to {OUT}")


if __name__ == "__main__":
    main()
