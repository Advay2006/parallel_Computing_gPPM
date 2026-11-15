#!/usr/bin/env python3
"""Create submission PDF and a verified ZIP of the working repository."""
from hashlib import sha256
from pathlib import Path
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PDF = ROOT / "Final_Research_Paper.pdf"
ARCHIVE = ROOT / "Final_Code_Repository.zip"
SOURCE_PDF = ROOT / "docs/Milestone3_IEEE/main.pdf"
SKIP_DIRS = {".git", "build", "__pycache__"}
SKIP_FILES = {"main.aux", "main.log", "missfont.log"}


def main():
    if PDF.exists() or ARCHIVE.exists():
        raise SystemExit("Submission output already exists; move it aside before repackaging.")
    if not SOURCE_PDF.is_file():
        raise SystemExit("Build docs/Milestone3_IEEE/main.pdf first.")
    shutil.copyfile(SOURCE_PDF, PDF)
    paths = []
    for path in ROOT.rglob("*"):
        rel = path.relative_to(ROOT)
        if any(part in SKIP_DIRS for part in rel.parts) or path.is_symlink():
            continue
        if not path.is_file() or path == ARCHIVE or path.name in SKIP_FILES:
            continue
        if path.suffix in {".zip", ".pyc"}:
            continue
        paths.append(path)
    required = ["Makefile", "README.md", "src/milestone4/benchmark.c",
                "results/milestone4/final_32mib.csv", "results/milestone4/profiles/gpu_table_m2_s3.sqlite",
                "docs/Milestone3_IEEE/main.tex", "Final_Research_Paper.pdf"]
    members = {str(p.relative_to(ROOT)) for p in paths}
    if not set(required) <= members:
        raise RuntimeError(f"Missing required files: {set(required) - members}")
    manifest = []
    with zipfile.ZipFile(ARCHIVE, "x", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for path in sorted(paths):
            name = str(Path("final_code_repository") / path.relative_to(ROOT))
            content = path.read_bytes()
            z.writestr(name, content)
            manifest.append(f"{sha256(content).hexdigest()}  {name}\n")
        z.writestr("final_code_repository/SHA256SUMS.txt", "".join(manifest))
    with zipfile.ZipFile(ARCHIVE) as z:
        bad = z.testzip()
        if bad:
            raise RuntimeError(f"ZIP integrity failure: {bad}")
        for line in manifest:
            digest, name = line.strip().split("  ", 1)
            if sha256(z.read(name)).hexdigest() != digest:
                raise RuntimeError(f"ZIP checksum mismatch: {name}")
    print(f"Created {PDF.relative_to(ROOT)} and {ARCHIVE.relative_to(ROOT)}")
    print(f"Verified {len(paths)} files, CRCs, and SHA-256 hashes.")


if __name__ == "__main__":
    main()
