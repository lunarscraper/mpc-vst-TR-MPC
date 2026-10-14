#!/usr/bin/env python3
"""Build a module.json that mpc-vst-plugins' schwung adapter can read, from a kit's generated params header.

The Schwung kits keep chain_params and the page hierarchy as C string literals in src/dsp/*_params.h
(module.json is capped at 8 KB by Schwung's loader), so the adapter finds nothing in their module.json.

    extract_module.py ports/6w6/src/dsp/sd606_params.h ports/6w6/src/module.json -o ports/6w6/module.mpc.json
"""
import argparse
import json
import re
import sys


def literal(header, name):
    m = re.search(r"static const char %s\[\]\s*=\s*((?:\s*\"(?:[^\"\\]|\\.)*\")+)\s*;" % re.escape(name), header)
    if not m:
        sys.exit("%s not found" % name)
    parts = re.findall(r"\"((?:[^\"\\]|\\.)*)\"", m.group(1))
    return json.loads('"' + "".join(parts) + '"')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("header")
    ap.add_argument("module_json")
    ap.add_argument("-o", required=True)
    a = ap.parse_args()
    h = open(a.header, encoding="utf-8").read()
    chain = re.search(r"static const char (\w+_chain_params_json)\[\]", h).group(1)
    pages = re.search(r"static const char (\w+_ui_(?:pages|hierarchy)_json)\[\]", h).group(1)
    mod = json.load(open(a.module_json, encoding="utf-8"))
    mod["chain_params"] = json.loads(literal(h, chain))
    # STEMS (ports/common/mpc_pads_engine.c answers these, not the kit): always the last two, so no index moves
    mod["chain_params"] += [
        {"key": "stems", "name": "Stems", "type": "enum", "options": ["OFF", "DRY", "AS HEARD"], "default": 0},
        {"key": "stems_status", "name": "Stems Status", "min": 0, "max": 0, "display": "string", "type": "readout"},
    ]
    mod["ui_hierarchy"] = json.loads(literal(h, pages))
    json.dump(mod, open(a.o, "w", encoding="utf-8"), indent=1)
    print("%s: %d chain_params, levels: %s" % (a.o, len(mod["chain_params"]), list(mod["ui_hierarchy"].get("levels", {}))[:8]))


main()
