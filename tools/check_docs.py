"""Consistency checks for the project's engineering records.

Checks that evidence rows carry a class and a datasheet source, that every
cited ID is defined, that every requirement in AGENTS.md is traced, that the
datasheet checksum agrees everywhere it is recorded, that local links resolve,
and that project documentation contains no em or en dashes.

Uses only the standard library so it runs under any Python 3.9 or later,
without the tools/ environment. Exit status is 1 if any check fails.

    python3 tools/check_docs.py [--root PATH]
"""

import argparse
import hashlib
import re
import sys
from pathlib import Path

# Project documentation subject to the style and link checks.
DOC_FILES = ["README.md", "AGENTS.md", "reference/README.md", "tools/README.md"]
DOC_DIRS = ["docs"]

# Sources scanned for ID citations.
CITATION_GLOBS = ["*.md", "docs/**/*.md", "include/**/*.hpp", "src/**/*.cpp",
                  "src/**/*.hpp", "tests/**/*.cpp"]

# ID kinds: pattern and the file whose table rows define them.
ID_KINDS = {
    "D": (r"D-(?:[A-Z]\d{2}|\d{2})", "docs/evidence.md"),
    "I": (r"I-(?:[A-Z]\d{2}|AD\d{2})", "docs/evidence.md"),
    "AD": (r"AD-(?:A\d{2}|\d{2})", "docs/evidence.md"),
    "X": (r"X-\d{2}", "docs/evidence.md"),
    "DISC": (r"DISC-\d{2}", "docs/evidence.md"),
    "P": (r"P-\d{2}", "docs/assumptions.md"),
    "U": (r"U-\d{2}", "docs/assumptions.md"),
    "O": (r"O-\d{2}", "docs/api.md"),
}

REQ_PATTERN = r"REQ-[A-Z]+-\d{2}"
REQ_DEFINITION = re.compile(r"\*\*(" + REQ_PATTERN + r"):\*\*")

# Documents that must trace every requirement defined in AGENTS.md.
TRACE_FILES = ["docs/evidence.md", "docs/assumptions.md", "docs/api.md", "docs/features.md"]

SHA256 = re.compile(r"\b[0-9a-f]{64}\b")
CHECKSUM_FILES = ["AGENTS.md", "docs/evidence.md", "reference/README.md"]
DATASHEET = "reference/AK4522.PDF"

VALID_CLASSES = {"D", "I", "P", "M"}
ROW_ID = re.compile(r"[A-Z]+-[A-Z]?\d{2}")
PAGE_REF = re.compile(r"\bpp?\.\d+")
DASHES = re.compile("[–—]")
LINK = re.compile(r"\[[^\]]*\]\(([^)\s]+)\)")


def expand_ranges(text, pattern):
    """Return IDs matching pattern, expanding 'A-01 to A-04' into each member."""
    ids = set(re.findall(r"\b" + pattern + r"\b", text))
    range_re = re.compile(r"\b(" + pattern + r")\s+to\s+(" + pattern + r")\b")
    for start, end in range_re.findall(text):
        prefix_a, num_a = re.match(r"(.*?)(\d+)$", start).groups()
        prefix_b, num_b = re.match(r"(.*?)(\d+)$", end).groups()
        if prefix_a != prefix_b or int(num_b) < int(num_a):
            continue
        width = len(num_a)
        for n in range(int(num_a), int(num_b) + 1):
            ids.add(f"{prefix_a}{n:0{width}d}")
    return ids


def parse_tables(text):
    """Yield (line_number, header_cells, row_cells) for every markdown table row."""
    header = None
    for number, line in enumerate(text.splitlines(), start=1):
        stripped = line.strip()
        if not stripped.startswith("|"):
            header = None
            continue
        cells = [c.strip() for c in stripped.strip("|").split("|")]
        if header is None:
            header = cells
        elif all(re.fullmatch(r":?-+:?", c) for c in cells if c):
            continue
        else:
            yield number, header, cells


def column(header, row, name):
    for index, title in enumerate(header):
        if title.lower().startswith(name.lower()) and index < len(row):
            return row[index]
    return None


def check_evidence_rows(path, text):
    errors = []
    for number, header, row in parse_tables(text):
        row_id = row[0]
        if not ROW_ID.fullmatch(row_id):
            continue  # legend and summary tables
        cls = column(header, row, "Class")
        source = column(header, row, "Source")
        where = f"{path}:{number} {row_id}"
        if cls is not None:
            if cls not in VALID_CLASSES:
                errors.append(f"{where}: class '{cls}' is not one of D, I, P, M")
            if cls == "D" and not (source and PAGE_REF.search(source)):
                errors.append(f"{where}: documented fact has no page reference")
            derivation = column(header, row, "Derivation")
            if cls == "I" and not (source or derivation):
                errors.append(f"{where}: inferred fact has no source or derivation")
            test = column(header, row, "Test")
            if test is not None and not test:
                errors.append(f"{where}: empty test column (use n/a if none applies)")
        elif re.fullmatch(r"(X|DISC)-\d{2}", row_id):
            if not (source and PAGE_REF.search(source)):
                errors.append(f"{where}: no page reference")
    return errors


def defined_ids(root):
    defined = {}
    for kind, (pattern, file) in ID_KINDS.items():
        text = (root / file).read_text(encoding="utf-8")
        ids = set()
        for _, _, row in parse_tables(text):
            if re.fullmatch(pattern, row[0]):
                ids.add(row[0])
        defined[kind] = ids
    return defined


def citation_files(root):
    seen = set()
    for glob in CITATION_GLOBS:
        for path in sorted(root.glob(glob)):
            if path.is_file() and path not in seen and ".venv" not in path.parts:
                seen.add(path)
                yield path


def check_citations(root):
    errors = []
    defined = defined_ids(root)
    for kind, ids in defined.items():
        if not ids:
            errors.append(f"{ID_KINDS[kind][1]}: no {kind} IDs defined")
    agents = (root / "AGENTS.md").read_text(encoding="utf-8")
    requirements = set(REQ_DEFINITION.findall(agents))

    for path in citation_files(root):
        text = path.read_text(encoding="utf-8")
        rel = path.relative_to(root)
        for kind, (pattern, owner) in ID_KINDS.items():
            # AGENTS.md defines its own D-xx tables; evidence.md must still cover them.
            for cited in sorted(expand_ranges(text, pattern) - defined[kind]):
                errors.append(f"{rel}: cites {cited}, which is not defined in {owner}")
        for cited in sorted(expand_ranges(text, REQ_PATTERN) - requirements):
            errors.append(f"{rel}: cites {cited}, which is not defined in AGENTS.md")

    traced = set()
    for file in TRACE_FILES:
        traced |= expand_ranges((root / file).read_text(encoding="utf-8"), REQ_PATTERN)
    for missing in sorted(requirements - traced):
        errors.append(f"{missing}: defined in AGENTS.md but not traced in {', '.join(TRACE_FILES)}")
    return errors


def check_checksums(root):
    errors = []
    values = {}
    for file in CHECKSUM_FILES:
        found = set(SHA256.findall((root / file).read_text(encoding="utf-8")))
        if len(found) != 1:
            errors.append(f"{file}: expected exactly one SHA-256, found {len(found)}")
        values[file] = found
    distinct = set().union(*values.values())
    if len(distinct) > 1:
        errors.append(f"datasheet checksum differs between {', '.join(CHECKSUM_FILES)}")
    pdf = root / DATASHEET
    if pdf.is_file() and len(distinct) == 1:
        actual = hashlib.sha256(pdf.read_bytes()).hexdigest()
        if actual not in distinct:
            errors.append(f"{DATASHEET}: SHA-256 {actual} does not match the recorded checksum")
    return errors


def heading_anchors(text):
    anchors = set()
    for line in text.splitlines():
        match = re.match(r"#+\s+(.*)", line)
        if match:
            slug = re.sub(r"[^\w\- ]", "", match.group(1).strip().lower())
            anchors.add(slug.replace(" ", "-"))
    return anchors


def doc_files(root):
    files = [root / f for f in DOC_FILES]
    for directory in DOC_DIRS:
        files.extend(sorted((root / directory).rglob("*.md")))
    return [f for f in files if f.is_file()]


def check_doc_style_and_links(root):
    errors = []
    for path in doc_files(root):
        text = path.read_text(encoding="utf-8")
        rel = path.relative_to(root)
        for number, line in enumerate(text.splitlines(), start=1):
            if DASHES.search(line):
                errors.append(f"{rel}:{number}: em or en dash in project documentation")
        for target in LINK.findall(text):
            if re.match(r"[a-z]+:", target):
                continue
            file_part, _, anchor = target.partition("#")
            dest = (path.parent / file_part).resolve() if file_part else path
            if not dest.exists():
                errors.append(f"{rel}: link target '{target}' does not exist")
            elif anchor and dest.suffix == ".md":
                if anchor not in heading_anchors(dest.read_text(encoding="utf-8")):
                    errors.append(f"{rel}: anchor '#{anchor}' not found in {dest.name}")
    return errors


def run(root):
    errors = []
    evidence = root / "docs/evidence.md"
    errors += check_evidence_rows("docs/evidence.md", evidence.read_text(encoding="utf-8"))
    errors += check_citations(root)
    errors += check_checksums(root)
    errors += check_doc_style_and_links(root)
    return errors


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    args = parser.parse_args(argv)
    errors = run(args.root.resolve())
    for error in errors:
        print(f"error: {error}")
    print(f"check_docs: {len(errors)} problem(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
