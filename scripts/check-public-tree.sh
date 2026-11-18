#!/usr/bin/env bash
#
# check-public-tree.sh -- refuse to publish material that must stay private.
#
# Run before every commit (see AGENTS.md). Checks the file set that would be
# published: staged changes if any exist, otherwise the whole tracked tree.
#
# Exit status: 0 = clean, 1 = at least one violation.

set -uo pipefail

cd "$(git rev-parse --show-toplevel 2>/dev/null)" || {
    echo "not inside a git repository" >&2
    exit 2
}

fail=0
fail_msg() { printf '  FAIL  %s\n' "$1"; fail=1; }
info()     { printf '  ok    %s\n' "$1"; }

# ---------------------------------------------------------------- file set --
if ! git diff --cached --quiet 2>/dev/null; then
    scope="staged changes"
    mapfile -t files < <(git diff --cached --name-only --diff-filter=ACM)
else
    scope="tracked tree"
    mapfile -t files < <(git ls-files)
fi

echo "checking $scope (${#files[@]} files)"

# --------------------------------------------------- forbidden file classes --
# Anything that is, or is derived from, original game material and binaries.
forbidden_ext='^(exe|dll|so|dylib|com|elf|bin|img|iso|dat|lbm|tm|ssd|lev|4e2|wld|stp|cfg|sav|zip|7z|rar|pak|o|a|lib|obj|png|jpg|jpeg|gif|bmp|tga|wav|mid|xmi|ad|sam)$'

for f in "${files[@]}"; do
    [ -n "$f" ] || continue
    base=${f##*/}
    ext=""
    case "$base" in *.*) ext=${base##*.} ;; esac
    ext=$(printf '%s' "$ext" | tr '[:upper:]' '[:lower:]')

    if [[ "$ext" =~ $forbidden_ext ]]; then
        fail_msg "$f (forbidden file type: .$ext)"
        continue
    fi
done

# ------------------------------------------------------- binary content scan --
for f in "${files[@]}"; do
    [ -f "$f" ] || continue
    enc=$(file -b --mime-encoding -- "$f" 2>/dev/null || echo binary)
    case "$enc" in
        binary|"") fail_msg "$f (binary content)" ;;
    esac
done

# ---------------------------------------------------------------- size guard --
# Specifications and source are small; a megabyte means something went wrong.
for f in "${files[@]}"; do
    [ -f "$f" ] || continue
    size=$(wc -c <"$f" 2>/dev/null || echo 0)
    if [ "$size" -gt 1048576 ]; then
        fail_msg "$f ($((size / 1024)) KiB, larger than 1 MiB)"
    fi
done

# ------------------------------------------------------ private-path markers --
# Absolute local paths and private workspace names must never appear.
markers='/home/|EF20003DFX2|EF2000OLD|_ef2000\.elf'
for f in "${files[@]}"; do
    case "$f" in
        # This script names the markers it searches for.
        scripts/check-public-tree.sh) continue ;;
    esac
    [ -f "$f" ] || continue
    if LC_ALL=C grep -InE "$markers" -- "$f" >/dev/null 2>&1; then
        fail_msg "$f (mentions a private path or install name)"
        LC_ALL=C grep -InE "$markers" -- "$f" | head -3 | sed 's/^/          /'
    fi
done

# ------------------------------------------------------------- game data dir --
for dir in game-data ../game-data; do
    if [ -d "$dir" ] && [ -n "$(ls -A "$dir" 2>/dev/null)" ]; then
        fail_msg "$dir exists and is not empty (game data must stay outside the repo)"
    fi
done

# -------------------------------------------------------------------- verdict --
if [ "$fail" -ne 0 ]; then
    cat >&2 <<'EOF'

Refusing to publish the files above. See AGENTS.md:
game data, derived assets, binaries and decompiled material never enter this
repository. Remove them from the index and retry:

    git restore --staged <path>
    rm -rf <path>                     # if it is untracked residue
    git commit --amend               # if it is already in the last commit
EOF
    exit 1
fi

info "no forbidden files, binaries, oversized files or private paths"
exit 0
