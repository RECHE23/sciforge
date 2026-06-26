"""Minimal abi3 binding fixture: re-export the extension's one function."""
from pydemo._demo import answer

__all__ = ["answer"]
