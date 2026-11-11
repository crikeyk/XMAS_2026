#!/usr/bin/env python3
"""Turn the seed/ folder into a C array the firmware writes on a blank drive."""
import pathlib
import sys

seed = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])
files = sorted(
    p for p in seed.iterdir() if p.is_file() and p.suffix.lower() in {".txt", ".bmp"}
)
lines = [
    "#include <stdint.h>\n",
    "typedef struct { const char *name; const uint8_t *data; unsigned len; } SeedFile;\n",
]
for i, path in enumerate(files):
    data = path.read_bytes()
    lines.append("static const uint8_t seed_%d[] = {\n" % i)
    for j in range(0, len(data), 12):
        chunk = data[j : j + 12]
        lines.append("  " + ",".join(str(b) for b in chunk) + ",\n")
    lines.append("};\n")
lines.append("const SeedFile g_seed_files[] = {\n")
for i, path in enumerate(files):
    lines.append('  {"%s", seed_%d, sizeof seed_%d},\n' % (path.name, i, i))
lines.append("};\nconst unsigned g_seed_count = %d;\n" % len(files))
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text("".join(lines))
