import sys

import sciforge_build
from setuptools import Extension, setup

# abi3 (cp310): one built extension works on CPython 3.10+. The substrate headers come
# from the build-time-only sciforge-build package (build-system.requires). C++20 is the
# ecosystem baseline (it lets concepts/consteval constrain caster<T> in N3).
_std = ["/std:c++20"] if sys.platform == "win32" else ["-std=c++20"]

setup(
    ext_modules=[
        Extension(
            "bindingdemo._demo",
            sources=["bindingdemo/_demo.cpp"],
            include_dirs=[sciforge_build.get_include()],
            define_macros=[("Py_LIMITED_API", "0x030A0000")],
            py_limited_api=True,
            extra_compile_args=_std,
            language="c++",
        )
    ],
)
