# MapperBus project commands

build_dir := "buildDir"

# List available recipes
default:
    @just --list

# Configure the Meson build (run once or after meson.build changes)
setup:
    meson setup {{build_dir}} --reconfigure --wipe

# Build all targets
build:
    meson compile -C {{build_dir}}

# Run all tests
test: build
    meson test -C {{build_dir}}

# Run the SDL3 frontend (pass ROM path as argument)
run rom: build
    ./{{build_dir}}/src/app/mapperbus-sdl3 {{rom}}

# Run with xBRZ upscaling (scale: 2-6)
run-xbrz rom scale="3": build
    ./{{build_dir}}/src/app/mapperbus-sdl3 --scale {{scale}} {{rom}}

# Run with GPU-accelerated xBRZ upscaling (scale: 2-6)
run-gpu rom scale="3": build
    ./{{build_dir}}/src/app/mapperbus-sdl3 --scale {{scale}} --gpu {{rom}}

# Run the headless CLI frontend (pass ROM path as argument)
run-cli rom: build
    ./{{build_dir}}/src/frontends/cli/mapperbus-cli {{rom}}

# Format all C++ source files in-place
format:
    find src tests -name '*.hpp' -o -name '*.cpp' | sort | xargs clang-format -i

# Check formatting without modifying files (exits non-zero on diff)
format-check:
    find src tests -name '*.hpp' -o -name '*.cpp' | sort | xargs clang-format --dry-run --Werror

# Lint: compile with warnings + format check
lint: format-check build

# Run Clang Static Analyzer on all project source files
analyze:
    #!/usr/bin/env bash
    set -euo pipefail
    errors=0
    while IFS= read -r file; do
        echo "Analyzing $file..."
        if ! clang --analyze -std=c++23 -I src \
            $(pkg-config --cflags sdl3 2>/dev/null || true) \
            -Xanalyzer -analyzer-output=text \
            "$file" 2>&1; then
            errors=$((errors + 1))
        fi
    done < <(find src tests -name '*.cpp' | sort)
    rm -f *.plist
    if [ "$errors" -gt 0 ]; then
        echo "Analyzer found issues in $errors file(s)"
        exit 1
    fi
    echo "Static analysis passed"

# Run clang-tidy on all project source files (requires compile_commands.json)
tidy: build
    #!/usr/bin/env bash
    set -euo pipefail
    TIDY="$(command -v clang-tidy 2>/dev/null || echo /opt/homebrew/opt/llvm/bin/clang-tidy)"
    if [ ! -x "$TIDY" ]; then
        echo "clang-tidy not found"; exit 1
    fi
    SYSROOT="$(xcrun --show-sdk-path 2>/dev/null || true)"
    EXTRA_ARGS=()
    if [ -n "$SYSROOT" ]; then
        EXTRA_ARGS+=(--extra-arg="-isysroot$SYSROOT")
    fi
    find src -name '*.cpp' | sort | \
        xargs "$TIDY" -p {{build_dir}} --header-filter='src/.*' "${EXTRA_ARGS[@]}" 2>&1
    echo "clang-tidy passed"

# Clean the build directory
clean:
    rm -rf {{build_dir}}

# Full rebuild from scratch
rebuild: clean setup build
