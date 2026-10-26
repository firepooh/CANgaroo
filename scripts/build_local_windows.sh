#!/bin/bash
# Builds cangaroo with MSYS2 MinGW64 and collects a standalone copy in dist/.
# Called by Build_Local.cmd; run from a MINGW64 shell in the repository root.
#
# dist/ holds cangaroo.exe, the Qt runtime (windeployqt6), the MinGW runtime
# DLLs and the Python runtime, so it runs without MSYS2 in PATH.

set -e

# qmake parses the compiler's verbose output and runs it via cmd.exe
export LC_ALL=C
export COMSPEC="${COMSPEC:-C:\\Windows\\System32\\cmd.exe}"

# The MSYS2 login shell keeps only its own PATH; src/localversion reads the
# local version from git, so fall back to Git for Windows when MSYS2 has no git
if ! command -v git >/dev/null 2>&1 && [ -x "/c/Program Files/Git/cmd/git.exe" ]; then
    export PATH="$PATH:/c/Program Files/Git/cmd"
fi

# Elapsed time per step, so a slow build shows where the time goes
step_start=$SECONDS
step_done() { echo "--- $1: $((SECONDS - step_start)) s"; step_start=$SECONDS; }

cd "$(dirname "$0")/.."
ROOT=$(pwd)
DIST="$ROOT/dist"

# remote: localhost control endpoint for the MCP server in mcp/ (off unless --remote is given)
QMAKE_ARGS="CONFIG+=release CONFIG+=remote"

echo "=== Configure (qmake6) ==="
rm -f .qmake.stash src/.qmake.stash
# make does not rebuild objects when only compiler flags change, so start
# from scratch whenever the qmake arguments differ from the last build.
STAMP=build/.qmake_args
if [ "$(cat "$STAMP" 2>/dev/null)" != "$QMAKE_ARGS" ]; then
    echo "qmake arguments changed: clean build"
    rm -rf build/o build/moc build/ui build/rcc
fi
# -r regenerates src/Makefile too, so changed CONFIG options are not ignored
qmake6 -r $QMAKE_ARGS
mkdir -p build
echo "$QMAKE_ARGS" > "$STAMP"
step_done "configure"

echo "=== Build ==="
mingw32-make -j"$(nproc)"
step_done "compile"

echo "=== Deploy to dist ==="
rm -rf "$DIST"
mkdir -p "$DIST"
cp bin/cangaroo.exe "$DIST/"

windeployqt6 --release --no-system-d3d-compiler --no-translations "$DIST/cangaroo.exe"
step_done "windeployqt"

# Python runtime: libpython + stdlib under lib/pythonX.Y/.
# PythonEngine sets PYTHONHOME to the exe folder when it finds lib/pythonX.Y/os.py there.
PYVER=$(python3 -c "import sysconfig; print(sysconfig.get_config_var('VERSION'))")
PYLIB="$DIST/lib/python$PYVER"
cp "/mingw64/bin/libpython$PYVER.dll" "$DIST/"
mkdir -p "$PYLIB"
tar -C "/mingw64/lib/python$PYVER" \
    --exclude=site-packages --exclude=test --exclude=tests --exclude=__pycache__ \
    --exclude=idlelib --exclude=tkinter --exclude=turtledemo \
    -cf - . | tar -C "$PYLIB" -xf -
step_done "python runtime"

# MinGW runtime DLLs needed by the exe, the Qt plugins and the Python extension modules.
# One ntldd run over all of them: starting ntldd and the text tools once per file
# (about 100 files) took close to 3 minutes, because MSYS2 starts processes slowly.
find "$DIST" -mindepth 2 \( -name '*.dll' -o -name '*.pyd' \) -print0 \
    | xargs -0 ntldd -R "$DIST/cangaroo.exe" 2>/dev/null \
    | grep -i 'mingw64' \
    | awk '{print $3}' \
    | sed -E 's|\\|/|g' \
    | sort -u \
    | while IFS= read -r dll; do
        name=${dll##*/}
        [ -f "$DIST/$name" ] || cp "$(cygpath -u "$dll")" "$DIST/"
      done

step_done "mingw dlls"

mkdir -p "$DIST/examples"
cp examples/*.py "$DIST/examples/" 2>/dev/null || true

echo "=== Done ==="
du -sh "$DIST"
