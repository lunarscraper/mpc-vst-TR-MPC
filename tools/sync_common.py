#!/usr/bin/env python3
"""Copy ports/common/mpc_pads_engine.c, stemrec.h and the vendored engine.h into each Schwung-kit port's src/ and point its vst.json at them.

A port builds in a container that only sees its own folder, so the shared shim is copied, not referenced.
"""
import json
import os
import shutil

ports = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "ports")
for kit in ("6w6", "8w8", "cw78", "9w9"):
    src = os.path.join(ports, kit, "src")
    for f in ("mpc_pads_engine.c", "stemrec.h", "engine.h"):
        shutil.copy(os.path.join(ports, "common", f), os.path.join(src, f))
    p = os.path.join(ports, kit, "vst.json")
    v = json.load(open(p))
    v.pop("module", None)
    v["params"] = "module.mpc.json"          # a Schwung-shaped file; params.py reads it through the adapter's reader
    s = v["build"]["sources"]
    if "src/mpc_pads_engine.c" not in s:
        s.append("src/mpc_pads_engine.c")
    libs = v["build"].setdefault("libs", [])
    if "-lpthread" not in libs:              # STEMS: the recorder's own thread (stemrec.h)
        libs.append("-lpthread")
    json.dump(v, open(p, "w", newline="\n"), indent=2)
    print(kit, "ok")
