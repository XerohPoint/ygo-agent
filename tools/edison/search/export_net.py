#!/usr/bin/env python3
"""Write an Edison checkpoint as the flat binary that edison_net.h loads.

Reads the checkpoint through actor.load_checkpoint, so format, hash and code list all
fail closed first. The file holds the config, every array's name and shape, the source
params_sha256 and the code list hash. Any mismatch at load refuses the file. The bin
tables are written from net.py rather than recomputed natively.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "learner"))
from obs import HINT_MAX, HINT_MIN, MAX_OPTIONS, MAX_ROWS  # noqa: E402

MAGIC = b"EDNET01\n"
FORMAT_VERSION = 1
NAME_BYTES = 72
HEADER = struct.Struct("<8s2I6I6IQ32s32s32s32s")
ENTRY = struct.Struct(f"<{NAME_BYTES}s4I2Q")
assert HEADER.size == 200 and ENTRY.size == 104


def flatten(tree, prefix=""):
    for key in sorted(tree):
        if isinstance(tree[key], dict):
            yield from flatten(tree[key], f"{prefix}{key}/")
        else:
            yield f"{prefix}{key}", tree[key]


def serialize(arrays, config, params_sha256, codes_sha256):
    """arrays: [(name, float32 array)]. Returns the file's bytes."""
    table, chunks, offset = [], [], 0
    for name, array in arrays:
        array = np.asarray(array)
        if array.dtype != np.float32 or array.ndim not in (1, 2) or not np.isfinite(array).all():
            raise ValueError(f"Parameter {name} is not a finite float32 vector or matrix")
        encoded = name.encode()
        if len(encoded) >= NAME_BYTES:
            raise ValueError(f"Parameter name too long: {name}")
        shape = list(array.shape) + [0] * (3 - array.ndim)
        table.append(ENTRY.pack(encoded, array.ndim, *shape, offset, array.size))
        data = array.tobytes()
        chunks.append(data + b"\0" * (-len(data) % 64))
        offset += len(chunks[-1])
    payload = b"".join(chunks)
    header_bytes = HEADER.size + ENTRY.size * len(table)
    header_bytes += -header_bytes % 64

    def pack_header(header_sha):
        return HEADER.pack(MAGIC, FORMAT_VERSION, header_bytes,
                           config["n_codes"], config["id_dim"], config["channels"], config["layers"], config["heads"],
                           config["critic_hidden"], MAX_ROWS, MAX_OPTIONS, HINT_MIN, HINT_MAX, 32, len(table), len(payload),
                           bytes.fromhex(params_sha256), bytes.fromhex(codes_sha256), hashlib.sha256(payload).digest(),
                           header_sha)

    def assemble(header_sha):
        head = pack_header(header_sha) + b"".join(table)
        return head + b"\0" * (header_bytes - len(head))

    head = assemble(hashlib.sha256(assemble(b"\0" * 32)).digest())
    return head + payload


def export(stem, output, expected_sha=None):
    from actor import load_checkpoint
    from net import BIN_POINTS, BIN_WIDTHS
    config, params, meta, sha = load_checkpoint(stem, expected_sha)
    arrays = [("const/bin_points", BIN_POINTS.astype(np.float32)), ("const/bin_widths", BIN_WIDTHS.astype(np.float32))]
    arrays += list(flatten(params))
    Path(output).write_bytes(serialize(arrays, config.dict(), sha, meta["codes_sha256"]))
    return {"params_sha256": sha, "codes_sha256": meta["codes_sha256"], "config": config.dict(),
            "file_sha256": hashlib.sha256(Path(output).read_bytes()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkpoint", help="Checkpoint stem, with no suffix")
    parser.add_argument("output", type=Path)
    parser.add_argument("--expect-sha", help="Refuse unless the source params_sha256 is this")
    args = parser.parse_args()
    print(json.dumps(export(args.checkpoint, args.output, args.expect_sha), indent=1))


if __name__ == "__main__":
    main()
