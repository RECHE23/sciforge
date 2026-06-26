from setuptools import Extension, setup

# abi3: pin the limited API to 3.10 so one built extension works on CPython 3.10+.
setup(
    ext_modules=[
        Extension(
            "pydemo._demo",
            sources=["pydemo/_demo.c"],
            define_macros=[("Py_LIMITED_API", "0x030A0000")],
            py_limited_api=True,
        )
    ],
)
