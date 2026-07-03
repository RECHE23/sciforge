# MISRA C++:2023 deviations

SciForge **owns** the shared clang-tidy MISRA profile the ecosystem uses: `lint/clang-tidy-misra`. It
covers a material subset of MISRA C++:2023 through the cppcoreguidelines, cert, bugprone, hicpp and misc
modules. REAL and SciLex run it (`make misra`) against their own headers; each documents its own
deviations in its `MISRA.md`.

## The profile is self-tested

`make lint-config` self-tests the profile: it must parse, and it must still *behave* — an enabled check
fires on a crafted violation, a deliberately-disabled one does not. So the shared base cannot silently
rot into permissiveness.

## SciForge's own headers

The test-harness, benchmark, and binding-helper headers carry **zero in-source suppressions**: they
satisfy the profile as written. The deviations the profile disables (the idiomatic header-library
conflicts — magic numbers intrinsic to the code, pointer walks, C arrays for constexpr tables, public
data members on small POD aggregates, and so on) are the shared ones documented in the consumers'
`MISRA.md`; SciForge adds none of its own.
