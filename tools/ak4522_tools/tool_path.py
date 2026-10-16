"""Locate a built executable in a CMake build directory on any platform:
single-configuration generators put it at the top level; Visual Studio and
Xcode put it under a configuration folder, with .exe on Windows."""

from __future__ import annotations

from pathlib import Path


def find(build: Path, name: str) -> Path:
    candidates = [build / name, build / f"{name}.exe"]
    for config in ("Release", "RelWithDebInfo", "Debug"):
        candidates += [build / config / name, build / config / f"{name}.exe"]
    for path in candidates:
        if path.is_file():
            return path
    raise FileNotFoundError(f"{name} not found in {build}; build the tools target first")
