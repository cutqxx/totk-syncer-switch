"""Independent reference implementation of the save fingerprint.

Usage: python -I contracts/fingerprint.py contracts/save-tree
Prints: sha256:<hex>

Definition (spec "Отпечаток сейва"): SHA-256 over the lines
"<relative path with />\n<hex sha256 of file content>\n" for every save file,
sorted by path as UTF-8 bytes. Junk files (by file name, case-insensitive) and
directories are skipped.
"""
import hashlib
import os
import sys

JUNK_NAMES = {".nx_save_meta.bin", "desktop.ini", "thumbs.db"}
JUNK_SUFFIXES = (".7z", ".zip", ".rar")


def is_junk(file_name):
    lowered = file_name.lower()
    return lowered in JUNK_NAMES or lowered.endswith(JUNK_SUFFIXES)


def save_files(root):
    for folder, _, names in os.walk(root):
        for name in names:
            if not is_junk(name):
                full = os.path.join(folder, name)
                yield os.path.relpath(full, root).replace(os.sep, "/"), full


def fingerprint(root):
    outer = hashlib.sha256()
    for rel, full in sorted(save_files(root), key=lambda item: item[0].encode("utf-8")):
        with open(full, "rb") as handle:
            content_hash = hashlib.sha256(handle.read()).hexdigest()
        outer.update(f"{rel}\n{content_hash}\n".encode("utf-8"))
    return "sha256:" + outer.hexdigest()


if __name__ == "__main__":
    print(fingerprint(sys.argv[1]))
