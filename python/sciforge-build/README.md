# sciforge-build

Build-time distribution of the SciForge C++ **binding substrate** headers
(`<sciforge/binding/...>`). It is **never a runtime dependency** — list it in your
`build-system.requires` and add `sciforge_build.get_include()` to your extension's
`include_dirs` at wheel-build time. Pure-Python (`py3-none-any`); CalVer, aligned with
the SciForge tag.
