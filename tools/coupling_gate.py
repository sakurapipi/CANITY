"""Coupling budgets with stable rule/target identities and numeric debt ceilings.

Existing textual domain/file baselines remain readable. New baselines use JSON.
Function discovery reuses the project's lexical C reader; it is not Clang AST
and does not claim to expand macros or select active preprocessor branches.
"""
import argparse
from dataclasses import dataclass
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from c_source import functions

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUDGET = 2500
FILE_LIMIT = 2600
FUNC_HARD = 300
SOFT_200 = 10
EXEMPT_FILES = {"src/cjk_glyphs.c"}  # Generated glyph data, not handwritten logic.
WEB_ONLY = {
    "audio_web.c", "gamepad_web.c", "music_web.c",
    "platform/main_web.c", "platform/plat_web.c", "steam/steam_zone_web.c",
}
NUMERIC_RULES = {"domain_cost", "file_lines", "function_lines", "large_function_count"}


@dataclass(frozen=True)
class Violation:
    rule: str
    target: str
    value: int
    limit: int
    message: str

    @property
    def key(self):
        return self.rule, self.target.replace("\\", "/")


def load_baseline(path):
    text = Path(path).read_text(encoding="utf-8-sig")
    stripped = text.lstrip()
    if stripped.startswith("{"):
        payload = json.loads(text)
        if not isinstance(payload, dict) or payload.get("schema") != 1 or not isinstance(payload.get("limits"), list):
            raise ValueError("baseline requires schema=1 and a limits array")
        entries = payload["limits"]
    else:
        entries = []
        patterns = (
            (r"域 (.+) 超预算：(\d+) > \d+（最大文件 \d+）", "domain_cost"),
            (r"文件 (.+) 有 (\d+) 行 > \d+", "file_lines"),
        )
        for line in text.splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            for pattern, rule in patterns:
                match = re.fullmatch(pattern, line)
                if match:
                    entries.append(dict(rule=rule, target=match[1], max_value=int(match[2])))
                    break
            else:
                raise ValueError("unsupported legacy baseline entry: " + line)
    baseline = {}
    for entry in entries:
        if not isinstance(entry, dict):
            raise ValueError("baseline limits must contain objects")
        rule, target, value = entry.get("rule"), entry.get("target"), entry.get("max_value")
        if not isinstance(rule, str) or rule not in NUMERIC_RULES or not isinstance(target, str) or not target:
            raise ValueError("invalid baseline rule or target")
        if type(value) is not int or value < 0:
            raise ValueError("baseline max_value must be a nonnegative integer")
        key = rule, target.replace("\\", "/")
        if key in baseline:
            raise ValueError("duplicate baseline rule/target: " + str(key))
        baseline[key] = value
    return baseline


def split_debt(violations, baseline):
    known, fresh = [], []
    for item in violations:
        ceiling = baseline.get(item.key)
        if ceiling is not None and item.value <= ceiling:
            known.append(item)
        else:
            fresh.append(item)
    return known, fresh


def count_lines(path):
    try:
        return len(Path(path).read_text(encoding="utf-8", errors="replace").splitlines())
    except OSError:
        return 0


def module_cost_module():
    path = os.path.join(ROOT, "tools", "module_cost.py")
    spec = importlib.util.spec_from_file_location("module_cost", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def function_inventory():
    out = []
    for path in sorted((Path(ROOT) / "src").rglob("*.c")):
        rel = path.relative_to(ROOT).as_posix()
        if rel in EXEMPT_FILES:
            continue
        for function in functions(path.read_text(encoding="utf-8", errors="replace")):
            out.append((rel, function.name, function.line, function.lines))
    return out


def function_sizes():
    # Preserve the public tuple shape used by older maintenance callers.
    return [(rel, line, size) for rel, _name, line, size in function_inventory()]


def registered_sources():
    result = subprocess.run([sys.executable, "-X", "utf8", "tools/build_sources.py"],
                            cwd=ROOT, capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    if result.returncode:
        raise RuntimeError("source registry failed: " + result.stderr)
    out = set()
    for line in result.stdout.splitlines():
        line = line.strip().replace("\\", "/")
        if line.endswith(".c"):
            out.add(line[4:] if line.startswith("src/") else line)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--baseline", type=Path)
    args = parser.parse_args()
    try:
        baseline = load_baseline(args.baseline if args.baseline.is_absolute()
                                 else Path(ROOT) / args.baseline) if args.baseline else {}
        fails = []
        for row in module_cost_module().measure():
            cost = row["cost"]
            if cost > BUDGET:
                fails.append(Violation("domain_cost", row["domain"], cost, BUDGET,
                    "域 %s 超预算：%d > %d（最大文件 %d）" %
                    (row["domain"], cost, BUDGET, row["biggest"])))
            if not args.quiet:
                print("domain %-12s cost=%d limit=%d" % (row["domain"], cost, BUDGET))
        for path in sorted((Path(ROOT) / "src").rglob("*.c")):
            rel = path.relative_to(ROOT).as_posix()
            if rel in EXEMPT_FILES:
                continue
            size = count_lines(path)
            if size > FILE_LIMIT:
                fails.append(Violation("file_lines", rel, size, FILE_LIMIT,
                                       "文件 %s 有 %d 行 > %d" % (rel, size, FILE_LIMIT)))
        inventory = function_inventory()
        for rel, name, line, size in inventory:
            if size > FUNC_HARD:
                fails.append(Violation("function_lines", rel + "::" + name, size, FUNC_HARD,
                    "函数过大：%s:%d %s 有 %d 行 > %d" % (rel, line, name, size, FUNC_HARD)))
        over200 = sum(size > 200 for _rel, _name, _line, size in inventory)
        if over200 > SOFT_200:
            fails.append(Violation("large_function_count", "src", over200, SOFT_200,
                "超过 200 行的函数有 %d 个 > 软上限 %d" % (over200, SOFT_200)))
        registered = registered_sources()
        for path in sorted((Path(ROOT) / "src").rglob("*.c")):
            rel = path.relative_to(Path(ROOT) / "src").as_posix()
            if rel not in WEB_ONLY and rel not in registered:
                fails.append(Violation("unregistered_source", "src/" + rel, 1, 0,
                                       "未登记进 build_sources：src/" + rel))
        known, fresh = split_debt(fails, baseline)
        print("解耦闸门：已知欠账 %d 项，新增或恶化 %d 项" % (len(known), len(fresh)))
        for item in known:
            print("  (已知) " + item.message + "；基线最大值=" + str(baseline[item.key]))
        for item in fresh:
            print("  (新增/恶化) " + item.message)
        print("解耦闸门：" + ("FAIL" if fresh else "PASS"))
        return 1 if fresh else 0
    except (OSError, ValueError, RuntimeError) as error:
        print("解耦闸门：ERROR — " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
