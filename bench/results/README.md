# Benchmark results

ArrayBook (direct-indexed price levels over a pooled intrusive FIFO) vs ReferenceBook
(`std::map` levels + `std::list` FIFO + `std::unordered_map` id index).

**Authorship:** ArrayBook is Robbie's. ReferenceBook, the bench harness (`bench/`), the
differential and support tests, and `bench/run.sh` were agent-written (Claude) and reviewed
by Robbie.

## Reproduce

```sh
bench/run.sh batch   # appends 3 runs to bench/results/<date>-m1pro.csv
```

`run.sh` refuses to run with uncommitted changes in `src/ include/ bench/`, stamps each row
with `git rev-parse --short HEAD`, and writes `<date>-<machine>.env` (CPU, OS, compiler).

## Machine

Apple M1 Pro, macOS 26.7.1, Apple clang 21.0.0 (clang-2100.3.34.2), Release preset
(`-O3`, no `-march`), on AC power. No core pinning: macOS has no user-space affinity API.

## Workload

- Seeded (`--seed 42`), `std::mt19937_64` with `rng() % range` and hand-rolled Fisher-Yates,
  so streams are identical on libc++ and libstdc++.
- n = 10000 resting orders, either side; buys at mid - [1..50] ticks, sells at
  mid + [1..50], mid 10000, band 9000-11000. Qty in [2..100].
- **Non-crossing; no matching.** Neither book matches yet; numbers cover add / cancel /
  modify only.
- ArrayBook is `ArrayBook<16384>` (pool of 16384 slots; default 10000).
- ReferenceBook is constructed with `expectedOrders = 16384`, so its `unordered_map` id
  index is `reserve`d once, like ArrayBook's preallocated pool; no rehash in timed code.
  Its `std::map` level nodes and `std::list` order nodes still come from system malloc
  per insert.

## Method: batch (`method=batch`)

Per trial, each book runs four phases over the same workload, each timed as one
`steady_clock` pair, ns/op = phase time / n:

1. `add` all n orders
2. `modify_px`: each order to a new same-side, non-crossing price (loses priority:
   unlink + relink at back)
3. `modify_qty_down`: each order qty - 1 at the same price (in place)
4. `cancel` all, in shuffled order

- Fresh book per trial (`make_unique`, construction untimed), so every trial starts from
  the same state.
- Books interleave A, R, A, R ... so drift (thermal, background load) hits both equally.
- Trial 0 of each book is untimed warmup (page faults, malloc arena for the map book).
- 31 timed trials per run; CSV reports median / min / max per book x op, plus
  `book=ratio` rows = ReferenceBook median / ArrayBook median.
- DCE guard: every returned id and the post-phase best bid + best ask are folded into a
  `volatile` sink, printed to stderr.
- Timer: `steady_clock` ticks at 41.67 ns on this machine (`mach_timebase_info` 125/3);
  batches of 10000 ops make that error < 0.01 ns/op.

## CSV columns

`date,machine,compiler,commit,run,book,op,stat,value,unit,n,trials,seed,method,block`

## Results

Pending: timed runs not yet recorded.
