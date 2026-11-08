"""Generator of the save ZIP test vectors (contracts/save-zip/*.zip and cases.json).

Usage: python -I contracts/save-zip/generate.py

Independent of the launcher code: uses only the Python standard library.
The output is deterministic (fixed entry timestamps), so a re-run changes nothing in git.
"""
import json
import os
import struct
import warnings
import zipfile

warnings.filterwarnings("ignore", message="Duplicate name")

HERE = os.path.dirname(os.path.abspath(__file__))
SAVE_TREE = os.path.join(HERE, "..", "save-tree")
TOTK_TITLE_ID = 0x0100F2C0115B6000
FIXED_TIME = (2026, 11, 3, 0, 0, 0)
MEBIBYTE = 1024 * 1024

MINIMAL = [("option.sav", b"option"), ("slot_00/progress.sav", b"progress")]

cases = []


def jksv_meta(title_id):
    body = b"JKSV" + b"\x01" + struct.pack("<Q", title_id)
    return body + bytes(86 - len(body))


def info_for(name, data, external_attr=0, compress_type=zipfile.ZIP_DEFLATED):
    info = zipfile.ZipInfo("x", FIXED_TIME)
    info.filename = name
    info.compress_type = compress_type
    info.create_system = 3
    info.external_attr = external_attr or (0o40755 << 16 if name.endswith("/") else 0o100644 << 16)
    return info


def write_zip(file_name, entries, compress_type=zipfile.ZIP_DEFLATED):
    path = os.path.join(HERE, file_name)
    with zipfile.ZipFile(path, "w") as archive:
        for entry in entries:
            name, data = entry[0], entry[1]
            external_attr = entry[2] if len(entry) > 2 else 0
            archive.writestr(info_for(name, data, external_attr, compress_type), data)
    return path


def add_valid(file_name, entries, saved_files, saved_dirs=()):
    write_zip(file_name, entries)
    cases.append({
        "file": file_name,
        "valid": True,
        "reason": None,
        "saved_files": sorted(saved_files, key=lambda name: name.encode("utf-8")),
        "saved_dirs": sorted(saved_dirs),
    })


def add_invalid(file_name, entries, reason, compress_type=zipfile.ZIP_DEFLATED):
    write_zip(file_name, entries, compress_type)
    cases.append({"file": file_name, "valid": False, "reason": reason})


def save_tree_entries():
    entries, files, dirs = [], [], []
    for folder, folder_names, names in os.walk(SAVE_TREE):
        folder_names.sort()
        rel_folder = os.path.relpath(folder, SAVE_TREE).replace(os.sep, "/")
        if rel_folder != ".":
            dirs.append(rel_folder)
        for name in sorted(names):
            if name == ".nx_save_meta.bin":
                continue
            rel = name if rel_folder == "." else rel_folder + "/" + name
            with open(os.path.join(folder, name), "rb") as handle:
                entries.append((rel, handle.read()))
    return entries, dirs


def build_valid_cases():
    tree_entries, tree_dirs = save_tree_entries()
    all_dirs = sorted(tree_dirs + ["album"])
    tree_files = [name for name, _ in tree_entries]
    junk = {"Thumbs.db", "desktop.ini", "x.zip", "picturebook/THUMBS.DB"}
    saved = [name for name in tree_files if name not in junk]
    add_valid("valid-save-tree.zip", [(d + "/", b"") for d in all_dirs] + tree_entries, saved, all_dirs)
    add_valid("valid-minimal.zip", MINIMAL, [name for name, _ in MINIMAL])
    add_valid("valid-empty-dirs.zip", [("album/", b"")] + MINIMAL, [name for name, _ in MINIMAL], ["album"])
    junk_entries = [("desktop.ini", b"x"), ("Thumbs.db", b"x"), ("x.zip", b"x"), ("slot_00/THUMBS.DB", b"x")]
    add_valid("valid-junk-extra.zip", MINIMAL + junk_entries, [name for name, _ in MINIMAL])
    meta = (".nx_save_meta.bin", jksv_meta(TOTK_TITLE_ID))
    add_valid("valid-jksv-meta.zip", [meta] + MINIMAL, [name for name, _ in MINIMAL])


def build_path_cases():
    add_invalid("invalid-abs-path.zip", MINIMAL + [("/evil.sav", b"x")], "unsafe path: /evil.sav")
    add_invalid("invalid-dotdot.zip", MINIMAL + [("../evil.sav", b"x")], "unsafe path: ../evil.sav")
    add_invalid("invalid-dotdot-inner.zip", MINIMAL + [("slot_00/../evil.sav", b"x")], "unsafe path: slot_00/../evil.sav")
    add_invalid("invalid-backslash.zip", MINIMAL + [("slot_01\\progress.sav", b"x")], "unsafe path: slot_01\\progress.sav")
    add_invalid("invalid-drive-letter.zip", MINIMAL + [("C:/evil.sav", b"x")], "unsafe path: C:/evil.sav")
    add_invalid("invalid-stream-name.zip", MINIMAL + [("option.sav:stream", b"x")], "unsafe path: option.sav:stream")
    add_invalid("invalid-reserved-name.zip", MINIMAL + [("slot_01/NUL", b"x")], "unsafe path: slot_01/NUL")
    add_invalid("invalid-trailing-dot.zip", MINIMAL + [("slot_01/caption.sav.", b"x")], "unsafe path: slot_01/caption.sav.")
    add_invalid("invalid-trailing-space.zip", MINIMAL + [("slot_01/caption.sav ", b"x")], "unsafe path: slot_01/caption.sav ")
    add_invalid("invalid-trailing-dot-dir.zip", MINIMAL + [("album./x.dat", b"x")], "unsafe path: album./x.dat")
    add_invalid("invalid-reserved-space-ext.zip", MINIMAL + [("slot_01/CON .txt", b"x")], "unsafe path: slot_01/CON .txt")
    add_invalid("invalid-reserved-spaces-ext.zip", MINIMAL + [("slot_01/nul  .dat", b"x")], "unsafe path: slot_01/nul  .dat")
    link = ("slot_01/link", b"option.sav", 0o120777 << 16)
    add_invalid("invalid-symlink.zip", MINIMAL + [link], "symlink entry: slot_01/link")


def build_duplicate_cases():
    add_invalid("invalid-dup.zip", MINIMAL + [("option.sav", b"again")], "duplicate path: option.sav")
    add_invalid("invalid-dup-case.zip", MINIMAL + [("Option.sav", b"again")], "duplicate path: Option.sav")
    add_invalid("invalid-dup-dir-case.zip", MINIMAL + [("Album/", b""), ("album/", b"")], "duplicate path: album")
    conflict = MINIMAL + [("album", b"a file"), ("album/x.sav", b"x")]
    add_invalid("invalid-file-and-dir.zip", conflict, "conflicting paths: album/x.sav")


def build_layout_cases():
    add_invalid("invalid-no-option.zip", [MINIMAL[1]], "missing option.sav")
    add_invalid("invalid-no-progress.zip", [MINIMAL[0], ("slot_00/caption.sav", b"x")], "missing slot_0N/progress.sav")
    wrapped = [("save/" + name, data) for name, data in MINIMAL]
    add_invalid("invalid-wrapped-root.zip", wrapped, "missing option.sav")
    add_invalid("invalid-slot-out-of-range.zip", [MINIMAL[0], ("slot_06/progress.sav", b"x")], "missing slot_0N/progress.sav")


def build_integrity_cases():
    with open(os.path.join(HERE, "invalid-not-zip.zip"), "wb") as handle:
        handle.write(b"this is not a zip file")
    cases.append({"file": "invalid-not-zip.zip", "valid": False, "reason": "not a zip file"})
    path = write_zip("invalid-crc.zip", MINIMAL, zipfile.ZIP_STORED)
    with open(path, "rb") as handle:
        raw = bytearray(handle.read())
    raw[raw.index(b"option.savoption") + len(b"option.sav")] ^= 0xFF
    with open(path, "wb") as handle:
        handle.write(raw)
    cases.append({"file": "invalid-crc.zip", "valid": False, "reason": "bad CRC: option.sav"})


def build_limit_cases():
    many = MINIMAL + [("picturebook/%04d.dat" % index, b"x") for index in range(999)]
    add_invalid("invalid-too-many-entries.zip", many, "too many entries (max 1000)")
    big = MINIMAL + [("picturebook/big%02d.bin" % index, bytes(MEBIBYTE)) for index in range(65)]
    add_invalid("invalid-too-large.zip", big, "unpacked size over 64 MiB")
    zeros = MINIMAL + [("picturebook/zeros.bin", bytes(8 * MEBIBYTE))]
    add_invalid("invalid-compression-bomb.zip", zeros, "suspicious compression ratio")


def build_meta_cases():
    other_title = (".nx_save_meta.bin", jksv_meta(0x0100000000001000))
    add_invalid("invalid-meta-other-title.zip", [other_title] + MINIMAL, "save of another game")
    bad_magic = (".nx_save_meta.bin", b"X" * 86)
    add_invalid("invalid-meta-bad-magic.zip", [bad_magic] + MINIMAL, "save of another game")


def main():
    build_valid_cases()
    build_path_cases()
    build_duplicate_cases()
    build_layout_cases()
    build_integrity_cases()
    build_limit_cases()
    build_meta_cases()
    with open(os.path.join(HERE, "cases.json"), "w", encoding="utf-8", newline="\n") as handle:
        json.dump(cases, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    print("wrote", len(cases), "cases")


if __name__ == "__main__":
    main()
