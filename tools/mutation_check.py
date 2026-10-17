"""Check that tests detect deliberate faults ("mutations") in the source code.

Each list in tools/mutations/ names its target and the tests to run in two
header lines, then gives a Python literal: a list of (name, old, new) tuples.

    # source: src/core/pcm.cpp
    # label: pcm
    [("ties round up", "return lowerIsEven ? lower : lower + 1.0;", "return lower + 1.0;"), ...]

For each entry the script replaces the single occurrence of `old` in the
source with `new`, rebuilds, and runs the CTest tests whose labels match the
label pattern (a regular expression, so "core|analog" selects both). It
reports DETECTED (some test failed), SURVIVED (all passed), BUILD FAILED, or
NOT APPLIED (`old` does not occur exactly once). The source is always
restored.

    python3 tools/mutation_check.py tools/mutations/pcm.txt [more lists] [--build DIR]
    python3 tools/mutation_check.py --all [--build DIR]
    python3 tools/mutation_check.py --check

--check only verifies that every list's source exists and every `old` still
occurs exactly once, without building; CTest runs it as `tools.mutation_lists`
so a refactor that leaves a list stale is noticed at once. Mutation runs are
slow (a few seconds per entry) and are not part of CTest.

Use a build configured with AK4522_WARNINGS_AS_ERRORS=OFF, or mutants that
leave unused code fail to build:

    cmake -S . -B build/mutation -DAK4522_WARNINGS_AS_ERRORS=OFF

Exit status is 1 if any mutation survived, failed to build, or failed to
apply, or if --check finds a stale entry.

make tracks file times at one-second resolution, so each write moves the
source's timestamp ahead and waits, guaranteeing a rebuild; without that a
mutant can be tested against the previous mutant's binary.
"""

import argparse
import ast
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LISTS = ROOT / "tools/mutations"
HEADER = re.compile(r"^#\s*(source|label):\s*(\S.*?)\s*$", re.MULTILINE)


def load(path):
    """Returns (source path, label pattern, mutations) of a list file."""
    text = path.read_text(encoding="utf-8")
    header = dict(HEADER.findall(text))
    if "source" not in header or "label" not in header:
        raise ValueError(f"{path.name}: missing '# source:' or '# label:' header")
    return ROOT / header["source"], header["label"], ast.literal_eval(text)


def write_with_new_stamp(path, text):
    path.write_text(text, encoding="utf-8")
    stamp = time.time() + 2
    os.utime(path, (stamp, stamp))
    time.sleep(2.1)


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def check_lists(paths):
    """The fast rot check: every entry must still apply exactly once."""
    problems = []
    for path in paths:
        try:
            source, _, mutations = load(path)
        except (ValueError, SyntaxError) as error:
            problems.append(str(error))
            continue
        if not source.is_file():
            problems.append(f"{path.name}: source {source.relative_to(ROOT)} does not exist")
            continue
        text = source.read_text(encoding="utf-8")
        for name, old, _ in mutations:
            matches = text.count(old)
            if matches != 1:
                problems.append(f"{path.name}: '{name}' matches {matches} times in {source.relative_to(ROOT)}")
    return problems


def run_list(path, build, config):
    """Runs one list; returns (problem count, survivor names)."""
    source, label, mutations = load(path)
    print(f"== {path.name}: {source.relative_to(ROOT)}, label {label}", flush=True)
    original = source.read_text(encoding="utf-8")
    ctest = ["ctest", "--test-dir", str(build), "-C", config, "-L", label]
    if run(["cmake", "--build", str(build), "-j", "--config", config]).returncode != 0 or run(ctest).returncode != 0:
        print("baseline build or tests fail; fix them before checking mutations", flush=True)
        return 1, []
    problems = 0
    survivors = []
    try:
        for name, old, new in mutations:
            matches = original.count(old)
            if matches != 1:
                print(f"NOT APPLIED ({matches} matches): {name}", flush=True)
                problems += 1
                continue
            write_with_new_stamp(source, original.replace(old, new))
            if run(["cmake", "--build", str(build), "-j", "--config", config]).returncode != 0:
                print(f"BUILD FAILED: {name}", flush=True)
                problems += 1
                continue
            tests = run(ctest)
            summary = next((l for l in tests.stdout.splitlines() if "tests passed" in l), "?")
            if tests.returncode == 0:
                problems += 1
                survivors.append(f"{path.name}: {name}")
            print(("DETECTED  " if tests.returncode else "SURVIVED  ") + f"{name}: {summary}", flush=True)
    finally:
        write_with_new_stamp(source, original)
        run(["cmake", "--build", str(build), "-j", "--config", config])
    return problems, survivors


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("lists", nargs="*", type=Path, help="mutation lists to run")
    parser.add_argument("--all", action="store_true", help="run every list in tools/mutations/")
    parser.add_argument("--check", action="store_true", help="only check that every list still applies")
    parser.add_argument("--build", type=Path, default=ROOT / "build/mutation")
    parser.add_argument("--config", default="Debug",
                        help="configuration for multi-config generators (Visual Studio, Xcode)")
    args = parser.parse_args(argv)

    every = sorted(LISTS.glob("*.txt"))
    if args.check:
        problems = check_lists(args.lists or every)
        for problem in problems:
            print(f"error: {problem}")
        print(f"mutation lists: {len(problems)} problem(s) in {len(args.lists or every)} list(s)")
        return 1 if problems else 0

    lists = every if args.all else args.lists
    if not lists:
        parser.error("give mutation lists, --all, or --check")
    total = 0
    survivors = []
    for path in lists:
        problems, survived = run_list(path, args.build, args.config)
        total += problems
        survivors += survived
    if len(lists) > 1:
        print(f"== {total} problem(s) in {len(lists)} list(s); survivors: {survivors or 'none'}")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
