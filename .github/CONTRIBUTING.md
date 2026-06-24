# Contributing

Thanks for your interest in contributing! This guide covers the basics.

## Reporting issues

- Search [existing issues](../../issues) before opening a new one.
- Use the bug report or feature request template.

## Development setup

This is a PlatformIO library. You need [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html) (or the VS Code extension).

```bash
# Install PlatformIO Core (requires Python)
pip install -U platformio
```

## Running tests

> [!IMPORTANT]
> Use the `*-test` environments for running unit tests with `pio test`.

If you have a board, you can run the unit tests on it:

```bash
# For example, for ESP32-S3:
pio test -e esp32-s3-test
```

Native unit tests run on the host (no board required):

```bash
pio test -e native-test
```

The CI also runs the sanitizer and coverage environments:

```bash
pio test -e native-san-test
pio test -e native-cov-test
```

## Verifying coverage

> [!IMPORTANT]
> For this special case, you need to run `pio run` instead of `pio test`, because the coverage report generation is a custom target, not a test.

The `native-cov-test` environment has a custom target that generates a coverage report in
`coverage/`, which you can view in your browser.

To verify coverage, you need to install `gcovr`:

```bash
pip install gcovr
```

Run the coverage report generation:

```bash
pio run -e native-cov-test -t coverage
```

## Compiling examples

> [!IMPORTANT]
> Use the `*-test` environments for compiling examples with `pio ci`.

```bash
# Arduino example (replace with your board env)
pio ci -l . -c platformio.ini -e esp32-s3-test examples/<example-folder>

# Native example
pio ci -l . -c platformio.ini -e native-test examples/<example-folder>
```

## Running examples

> [!IMPORTANT]
> Use the `*-example` environments for running examples with `pio run`.

Before running examples, you need to define the `EXAMPLE` environment variable to point to the example folder you want to run. For example, if you want to run the `examples/basic` example, you would set `EXAMPLE=examples/basic`.

```bash
# Arduino example (replace with your board env)
# - Windows:
$env:EXAMPLE="examples/basic"; pio run -e esp32-s3-example -t upload -t monitor
# - Linux
export EXAMPLE="examples/basic"; pio run -e esp32-s3-example -t upload -t monitor

# Native example
# - Windows:
$env:EXAMPLE="examples/basic"; pio run -e native-example -t exec
# - Linux
export EXAMPLE="examples/basic"; pio run -e native-example -t exec
```

## Code formatting

Code is formatted with [clang-format](https://clang.llvm.org/docs/ClangFormat.html) using the
`.clang-format` file at the repo root. CI enforces this and **fails if any file is not formatted**, so
format your changes before opening a PR.

> [!IMPORTANT]
> Use clang-format **17.0.6**, the exact version CI uses. Formatting output changes between major
> versions, so a different version may produce a diff CI rejects (or flag code that is actually fine).

The easiest way to match the version is the pinned pip package:

```bash
pip install clang-format==17.0.6
```

Format all sources in place:

```bash
clang-format -i $(git ls-files '*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hpp' '*.ino')
```

Alternatively, enable "format on save" in your editor pointed at clang-format 17.0.6.

## Commit and PR conventions

This project uses [Conventional Commits](https://www.conventionalcommits.org/). The **PR title** is what matters: it is used to auto-label the PR and to generate the release notes.

Accepted types: `feat`, `fix`, `docs`, `refactor`, `chore`, `ci`. Append `!` (e.g. `feat!:`) for
breaking changes.

Optionally, you can add a scope in parentheses after the type (e.g. `feat(mqtt):`).

Examples:

```md
# Add a new feature

feat: add non-blocking publish API

# Fix a bug with scope

fix(reconnect): avoid heap fragmentation

# Update documentation

docs: document the timeout option

# Refactor without changing the API

refactor: simplify the connection logic

# Breaking change by refactoring an API

refactor!: rename begin() to start()

# Breaking change by refactoring an API with scope

refactor(api)!: rename begin() to start()
```

## Pull request process

1. Fork the repo and create a branch from `main`.
2. Make your changes and keep them focused.
3. Format the code with clang-format, make sure tests pass and examples compile.
4. Open a PR with a Conventional Commit title and fill out the template.
