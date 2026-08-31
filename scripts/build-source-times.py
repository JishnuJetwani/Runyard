"""Give content-identical source files stable mtimes inside a BuildKit object cache.

COPY can restore a changed file with a timestamp older than its cached object.
Ninja needs that change to advance time even when the build context does not.
The BuildKit cache mount must be locked for the entire configure/build operation.
"""
import hashlib
import json
import os
import pathlib
import sys
import time


def synchronize(source, cache):
    manifest = cache / "runyard-source-times.json"
    previous = json.loads(manifest.read_text()) if manifest.exists() else {}
    current = {}
    for path in sorted(source.rglob("*")):
        if not path.is_file() or path.is_symlink():
            continue
        key = str(path.relative_to(source))
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        old = previous.get(key, {})
        timestamp = old["time_ns"] if old.get("sha256") == digest else time.time_ns()
        os.utime(path, ns=(timestamp, timestamp))
        current[key] = {"sha256": digest, "time_ns": timestamp}
    cache.mkdir(parents=True, exist_ok=True)
    temporary = manifest.with_suffix(".tmp")
    temporary.write_text(json.dumps(current, sort_keys=True))
    temporary.replace(manifest)


if __name__ == "__main__":
    synchronize(pathlib.Path(sys.argv[1]).resolve(), pathlib.Path(sys.argv[2]).resolve())
