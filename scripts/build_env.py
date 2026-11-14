# Defines NUT_BUILD_ENV (PlatformIO environment) and, for environments in
# release_package.BOARDS, NUT_RELEASE_ZIP_SUFFIX (suffix of the release zip).
import os
import sys

Import("env")  # noqa: F821

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(env["PROJECT_DIR"], "scripts"))  # noqa: F821
from release_package import BOARDS  # noqa: E402

pioenv = env["PIOENV"]  # noqa: F821
defines = [("NUT_BUILD_ENV", '\\"%s\\"' % pioenv)]
if pioenv in BOARDS:
    defines.append(("NUT_RELEASE_ZIP_SUFFIX", '\\"%s\\"' % BOARDS[pioenv]["zip_suffix"]))
env.Append(CPPDEFINES=defines)  # noqa: F821
