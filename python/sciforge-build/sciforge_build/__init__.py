"""Build-time access to the SciForge C++ binding-substrate headers.

Usage (in a consumer's setup.py):

    import sciforge_build
    Extension(..., include_dirs=[sciforge_build.get_include()])

so that `#include <sciforge/binding/error.hpp>` resolves at wheel-build time. This
package is never a runtime dependency.
"""
import os

__all__ = ["get_include"]


def get_include() -> str:
    """Return the directory to put on a C/C++ extension's include path.

    The returned path carries the ``sciforge/binding/`` header tree, so
    ``#include <sciforge/binding/error.hpp>`` resolves against it.
    """
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), "include")
