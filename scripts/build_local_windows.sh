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

cd "$(dirname "$0")/.."
ROOT=$(pwd)
DIST="$ROOT/dist"

echo "=== Configure (qmake6) ==="
rm -f .qmake.stash src/.qmake.stash
qmake6 CONFIG+=release

echo "=== Build ==="
mingw32-make -j"$(nproc)"

echo "=== Deploy to dist ==="
rm -rf "$DIST"
mkdir -p "$DIST"
cp bin/cangaroo.exe "$DIST/"

windeployqt6 --release --no-system-d3d-compiler --no-translations "$DIST/cangaroo.exe"

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

# MinGW runtime DLLs needed by the exe, the Qt plugins and the Python extension modules
copy_mingw_deps()
{
    ntldd -R "$@" 2>/dev/null \
        | grep -i 'mingw64' \
        | awk '{print $3}' \
        | sed -E 's|\\|/|g' \
        | sort -u \
        | while IFS= read -r dll; do
            name=$(basename "$dll")
            [ -f "$DIST/$name" ] || cp "$(cygpath -u "$dll")" "$DIST/"
          done
}

copy_mingw_deps "$DIST/cangaroo.exe"
find "$DIST" -mindepth 2 \( -name '*.dll' -o -name '*.pyd' \) | while IFS= read -r f; do
    copy_mingw_deps "$f"
done

mkdir -p "$DIST/examples"
cp examples/*.py "$DIST/examples/" 2>/dev/null || true

echo "=== Done ==="
du -sh "$DIST"
