"""Check that tests detect deliberate faults ("mutations") in a source file.

Each mutation list is a Python literal: a list of (name, old, new) tuples.
For each entry the script replaces the single occurrence of `old` with
`new`, rebuilds, runs the given CTest label, and reports DETECTED (some test
failed) or SURVIVED (all passed). The source file is always restored.

    python3 tools/mutation_check.py <source> <ctest-label> <mutation-list> [--build DIR]

Use a build directory configured with AK4522_WARNINGS_AS_ERRORS=OFF, or
mutants that leave unused code fail to build. Exit status is 1 if any
mutation survived or failed to apply.

make tracks file times at one-second resolution, so each write moves the
source's timestamp ahead and waits, guaranteeing a rebuild; without that a
mutant can be tested against the previous mutant's binary.
"""

import argparse
import ast
import os
import subprocess
import sys
import time
from pathlib import Path


def write_with_new_stamp(path, text):
    path.write_text(text, encoding="utf-8")
    stamp = time.time() + 2
    os.utime(path, (stamp, stamp))
    time.sleep(2.1)


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("source", type=Path)
    parser.add_argument("label")
    parser.add_argument("mutations", type=Path)
    parser.add_argument("--build", type=Path, default=Path("build/mutation"))
    parser.add_argument("--config", default="Debug",
                        help="configuration for multi-config generators (Visual Studio, Xcode)")
    args = parser.parse_args(argv)

    mutations = ast.literal_eval(args.mutations.read_text(encoding="utf-8"))
    original = args.source.read_text(encoding="utf-8")
    problems = 0

    baseline = run(["ctest", "--test-dir", str(args.build), "-C", args.config, "-L", args.label])
    if run(["cmake", "--build", str(args.build), "-j", "--config", args.config]).returncode != 0 or baseline.returncode != 0:
        print("baseline build or tests fail; fix them before checking mutations")
        return 1

    try:
        for name, old, new in mutations:
            matches = original.count(old)
            if matches != 1:
                print(f"NOT APPLIED ({matches} matches): {name}", flush=True)
                problems += 1
                continue
            write_with_new_stamp(args.source, original.replace(old, new))
            if run(["cmake", "--build", str(args.build), "-j", "--config", args.config]).returncode != 0:
                print(f"BUILD FAILED: {name}", flush=True)
                problems += 1
                continue
            tests = run(["ctest", "--test-dir", str(args.build), "-C", args.config, "-L", args.label])
            summary = next((l for l in tests.stdout.splitlines() if "tests passed" in l), "?")
            if tests.returncode == 0:
                problems += 1
            print(("DETECTED  " if tests.returncode else "SURVIVED  ") + f"{name}: {summary}",
                  flush=True)
    finally:
        write_with_new_stamp(args.source, original)
        run(["cmake", "--build", str(args.build), "-j", "--config", args.config])
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
