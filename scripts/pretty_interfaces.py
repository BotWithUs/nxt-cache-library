"""Pretty-print every interface-N.json under a directory in place.

Reads each file, reparses it with json.load, and rewrites it with indent=2.
Atomic per-file (write .tmp then rename) so a kill mid-run leaves files
either fully-pretty or fully-original, never half-written.
"""
import json
import os
import pathlib
import sys
import time

if len(sys.argv) < 2:
    print("usage: pretty_interfaces.py <dir>", file=sys.stderr)
    sys.exit(2)

root = pathlib.Path(sys.argv[1])
files = sorted(
    root.glob("interface-*.json"),
    key=lambda p: int(p.stem.split("-")[1]),
)
total = len(files)
print(f"found {total} files in {root}", flush=True)

t0 = time.time()
done = 0
bytes_in = 0
bytes_out = 0
for p in files:
    size_in = p.stat().st_size
    bytes_in += size_in
    try:
        with p.open("r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception as e:
        print(f"  SKIP {p.name}: parse failed ({e})", flush=True)
        continue

    tmp = p.with_suffix(".json.tmp")
    with tmp.open("w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
    bytes_out += tmp.stat().st_size
    os.replace(tmp, p)
    done += 1
    if done % 50 == 0:
        elapsed = time.time() - t0
        rate = done / elapsed if elapsed else 0
        eta = (total - done) / rate if rate else 0
        print(
            f"  {done}/{total}  "
            f"elapsed={elapsed:6.1f}s  "
            f"rate={rate:5.1f}/s  "
            f"eta={eta:6.0f}s",
            flush=True,
        )

elapsed = time.time() - t0
print(f"\nrewrote {done}/{total} files in {elapsed:.1f}s")
print(f"bytes in:  {bytes_in/1e9:.2f} GB")
print(f"bytes out: {bytes_out/1e9:.2f} GB  ({bytes_out/bytes_in:.2f}x)")
