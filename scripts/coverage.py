# SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
# SPDX-License-Identifier: MIT

# Coverage setup for the native-cov-test environment. Does two things:
# 1) Pass --coverage to the linker. PlatformIO/SCons (4.8.1) forwards -fsanitize to the linker
# automatically, but NOT --coverage (it has no special case in SCons ParseFlags, so it only reaches
# the compiler). Without this, the gcov runtime symbols (__gcov_*) are undefined at link time.
# 2) Add a custom "coverage" target that runs the tests and builds the report with gcovr.
# Use: pio run -e native-cov-test -t coverage  (output in coverage/ as XML and HTML).
import platform

Import("env")

env.Append(LINKFLAGS=["--coverage"])

env_name = env["PIOENV"]

coverage_dir_cmd = ""
platform_name = platform.system()
if platform_name == "Windows":
    coverage_dir_cmd = "if not exist coverage mkdir coverage"
else:
    coverage_dir_cmd = "mkdir -p coverage"

env.AddCustomTarget(
    name="coverage",
    dependencies=None,
    actions=[
        "pio test -e " + env_name,
        coverage_dir_cmd,
        "gcovr --root . --filter src/ .pio/build/"
        + env_name
        + " --print-summary"
        + " --xml coverage/coverage.xml --html-details coverage/index.html",
    ],
    title="Local coverage report",
    description="Unit tests + gcovr report (XML/HTML) in coverage/",
)
