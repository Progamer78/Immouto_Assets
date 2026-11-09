"""Group the entire converted game by command layout, without executing events.

The report is a work list, not a compatibility score: a dispatch handler can
still reject variants, and static inspection cannot establish reachability.
Dialogue, filenames and database contents are deliberately omitted.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

from prepare_game import COMMAND_NAMES, all_commands

ROOT = Path(__file__).resolve().parents[1]
# Literal packed mode words, not variable IDs or per-scene argument values.
MODE_ARGUMENT = {102: 0, 121: 3, 122: 1, 123: 1, 124: 1, 125: 0, 126: 0,
                 130: 4, 140: 0, 150: 0, 220: 0, 221: 3, 222: 3,
                 250: 3, 270: 0, 281: 0, 290: 0}


def dispatch_ids(source):
    body = source.split("void Runtime::execute(", 1)[1].split("switch (code) {", 1)[1]
    depth, found = 1, set()
    # Skip comments and strings, and exclude cases in nested mode switches.
    tokens = re.finditer(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|[{}]|case\s+(\d+)\s*:', body, re.S)
    for token in tokens:
        value = token.group()
        if value == "{":
            depth += 1
        elif value == "}":
            depth -= 1
            if not depth:
                return found
        elif token.group(1) and depth == 1:
            found.add(int(token.group(1)))
    raise ValueError("Cannot find the end of the runtime command switch")


def layout(command):
    code, args = command["code"], command["args"]
    result = {"numeric_arguments": len(args), "string_arguments": len(command["strings"])}
    index = MODE_ARGUMENT.get(code)
    if index is not None and index < len(args):
        result["mode_argument"] = index
        result["mode_word"] = args[index]
    elif code == 111 and args:
        result["branch_word"] = args[0]
        result["comparison_words"] = args[3::3]
    elif code == 112 and args:
        count = args[0] & 15
        result["branch_word"] = args[0]
        result["comparison_words"] = [value >> 24 & 255 for value in args[1:1 + count]]
    if "route" in command:
        result["route_flags"] = command["route"].get("flags")
    return result


def inventory(project, handlers):
    families, groups = {}, {}
    for context, command in all_commands(project):
        code = command["code"]
        family = families.setdefault(code, {"id": code, "name": COMMAND_NAMES.get(code, "Unknown"),
            "records": 0, "dispatch_handler_present": code in handlers, "variants": []})
        family["records"] += 1
        shape = layout(command)
        key = code, json.dumps(shape, sort_keys=True)
        if key not in groups:
            groups[key] = {**shape, "records": 0, "contexts": set(), "examples": []}
            family["variants"].append(groups[key])
        group = groups[key]
        group["records"] += 1
        group["contexts"].add(context)
        if len(group["examples"]) < 3:
            group["examples"].append({"context": context, "offset": command["offset"]})
    for family in families.values():
        for group in family["variants"]:
            group["context_count"] = len(group.pop("contexts"))
        family["variants"].sort(key=lambda group: -group["records"])
    return {"command_records": sum(f["records"] for f in families.values()),
        "command_ids": len(families), "layout_groups": len(groups),
        "missing_dispatch_handlers": [{key: f[key] for key in ("id", "name", "records")}
            for f in sorted(families.values(), key=lambda f: -f["records"]) if not f["dispatch_handler_present"]],
        "families": [families[code] for code in sorted(families)]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    parser.add_argument("--runtime", type=Path, default=ROOT / "src/runtime.cpp")
    parser.add_argument("--output", type=Path, default=ROOT / "reports/command_variants.json")
    args = parser.parse_args()
    data = args.project.read_bytes()
    source = args.runtime.read_bytes()
    report = {"format": "wolf-command-inventory", "version": 1,
        "project_sha256": hashlib.sha256(data).hexdigest(),
        "runtime_source_sha256": hashlib.sha256(source).hexdigest(),
        "limits": ["Static inventory; neither reachability nor full compatibility is established.",
            "A handler's presence does not mean its layouts, rendering or timing are supported.",
            "Groups retain arity and selected packed mode words; other value-dependent variants can exist.",
            "Move-route subcommands and text controls require separate implementation checks."],
        **inventory(json.loads(data), dispatch_ids(source.decode("utf8")))}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf8")
    print(json.dumps({key: report[key] for key in ("command_records", "command_ids", "layout_groups", "missing_dispatch_handlers")}, indent=2))


if __name__ == "__main__":
    main()
