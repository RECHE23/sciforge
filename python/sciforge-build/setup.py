"""Vendor the canonical <sciforge/binding/...> headers into the package at build time.

The headers live once, canonically, at the repo's include/sciforge/binding/. Copying
them under the package (rather than committing a second copy) keeps a single source of
truth while making the wheel + sdist self-contained — a consumer can build its
extension purely from `pip install sciforge-build`, with no sibling checkout.
"""
import pathlib
import shutil

from setuptools import setup

_HERE = pathlib.Path(__file__).resolve().parent
_CANONICAL = _HERE.parent.parent / "include" / "sciforge" / "binding"
_VENDORED = _HERE / "sciforge_build" / "include" / "sciforge" / "binding"

if _CANONICAL.is_dir():
    _VENDORED.parent.mkdir(parents=True, exist_ok=True)
    if _VENDORED.exists():
        shutil.rmtree(_VENDORED)
    shutil.copytree(_CANONICAL, _VENDORED)

setup()
