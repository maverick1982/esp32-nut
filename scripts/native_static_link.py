"""PlatformIO extra script of the native test environment (US-073).

Links libstdc++ and libgcc statically into the test programs. With the DLLs, the
std::string code runs inside libstdc++-6.dll and never calls the operator new a test
defines, so test_nut_table could not count String allocations. build_flags would only
reach the compiler: the flags go to LINKFLAGS here.
"""
Import("env")  # noqa: F821 (SCons global)

env.Append(LINKFLAGS=["-static-libstdc++", "-static-libgcc"])  # noqa: F821
