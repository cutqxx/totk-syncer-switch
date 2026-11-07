# contracts/

Shared test vectors for the C code (Switch) and the C# code (PC launcher).
Expected values are produced independently of the launcher code.

## save-tree/

Reference TotK save layout with tiny files, plus junk files that must be ignored:
`desktop.ini`, `Thumbs.db`, `.nx_save_meta.bin`, `x.zip` at the root and `picturebook/THUMBS.DB`.

`picturebook/` contains names whose order depends on the byte order of `-`, `.`, `/`, `_` and upper/lower case:
`B.dat`, `a-b.dat`, `a.dat`, `a/b.dat`, `a_b.dat`.

The `album/` directory of a real save is empty, and git cannot store empty directories.
It is deliberately NOT in this tree (a placeholder file would change the fingerprint and file list).
Tests that need an empty `album/` create it with the test helper `SaveTreeBuilder` (directories do not affect the fingerprint).

## save-tree.fingerprint.txt

`sha256:<hex>` of `save-tree/` per the spec definition (SHA-256 over `<relative path with />\n<hex sha256 of content>\n`
for every non-junk file, sorted by path as UTF-8 bytes, lowercase hex).

Regenerate with the independent script (no launcher code involved):

```
python -I contracts/fingerprint.py contracts/save-tree > contracts/save-tree.fingerprint.txt
```

Cross-check with plain shell tools (run inside `contracts/save-tree`):

```
find . -type f ! -iname desktop.ini ! -iname thumbs.db ! -iname .nx_save_meta.bin ! -iname '*.zip' | sed 's|^\./||' | LC_ALL=C sort \
  | while read -r f; do printf '%s\n%s\n' "$f" "$(sha256sum "$f" | cut -d' ' -f1)"; done | sha256sum
```

Both must print the same hex as the txt file. Files here are stored byte-exact (`contracts/** binary` in `.gitattributes`).
