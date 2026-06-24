# SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
# SPDX-License-Identifier: MIT

# Check that the EXAMPLE environment variable is defined and points to an existing folder. This
# variable is used to compile an example from the examples/ directory, and it needs to know which
# one to compile. If the variable is not defined or points to a non-existing folder, an error
# message is printed, and the build process is terminated.
import os
import sys

Import("env")

example = os.environ.get("EXAMPLE")

if not example:
    sys.stderr.write(
        "\n"
        "==================================================================================\n"
        " ERROR: the EXAMPLE environment variable is not defined.\n"
        " This env compiles an example from examples/<example> and needs to know which one.\n"
        " Define EXAMPLE before 'pio run', for example:\n"
        '   PowerShell: $env:EXAMPLE="examples/<example>"\n'
        '   bash/WSL:   export EXAMPLE="examples/<example>"\n'
        "==================================================================================\n"
    )
    env.Exit(1)
elif not os.path.isdir(os.path.join(env.subst("$PROJECT_DIR"), example)):
    sys.stderr.write(
        "\n"
        "==================================================================================\n"
        " ERROR: the example folder does not exist: EXAMPLE=" + example + "\n"
        " Check the value (relative path to the project root, e.g., examples/<example>)\n"
        " and make sure the folder exists.\n"
        "==================================================================================\n"
    )
    env.Exit(1)
