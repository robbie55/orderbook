#!/usr/bin/env bash
# Builds release, runs ob_bench 3x in one mode, appends CSV rows to
# bench/results/<date>-<machine>.csv. Usage: bench/run.sh [batch|timer|block]
# (timer runs once: it measures the clock, not the books.)
# MACHINE=<label> overrides the machine label (default m1pro).
set -euo pipefail

cd "$(dirname "$0")/.."
mode="${1:-batch}"
machine="${MACHINE:-m1pro}"

case "$mode" in
  batch) args=(--n 10000 --trials 31 --seed 42) ;;
  timer) args=(--timer-floor) ;;
  block) args=(--per-op --block 64 --n 10000 --trials 31 --seed 42) ;;
  *) echo "run.sh: unknown mode '$mode'" >&2; exit 2 ;;
esac

# Measured code must be committed so the CSV's commit column identifies it.
# results/ is excluded so earlier modes' uncommitted CSVs don't block later ones.
if [[ -n "$(git status --porcelain -- src include bench ':!bench/results')" ]]; then
  echo "run.sh: uncommitted changes in src/ include/ bench/; commit first" >&2
  exit 1
fi
commit="$(git rev-parse --short HEAD)"

if pgrep -fl 'ob_bench|bench/build|bench/run\.py' >&2; then
  echo "run.sh: warning: other bench-like processes running (above); timings may be noisy" >&2
fi

cmake --preset release >/dev/null
cmake --build --preset release >/dev/null

mkdir -p bench/results
out="bench/results/$(date +%F)-${machine}.csv"
[[ -f "$out" ]] || ./build/release/bench/ob_bench --header > "$out"

env_file="${out%.csv}.env"
if [[ ! -f "$env_file" ]]; then
  {
    echo "cpu: $(sysctl -n machdep.cpu.brand_string 2>/dev/null || grep -m1 'model name' /proc/cpuinfo | cut -d: -f2)"
    echo "os: $(sw_vers -productVersion 2>/dev/null || uname -sr)"
    echo "compiler: $(c++ --version | head -1)"
  } > "$env_file"
fi

runs=(1 2 3)
[[ "$mode" == timer ]] && runs=(1)
for run in "${runs[@]}"; do
  ./build/release/bench/ob_bench "${args[@]}" --run "$run" --commit "$commit" --machine "$machine" >> "$out"
done
echo "run.sh: $mode x3 @ $commit -> $out" >&2
