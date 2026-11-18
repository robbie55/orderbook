# Benchmark results

ArrayBook (direct-indexed price levels over a pooled intrusive FIFO) vs ReferenceBook
(`std::map` levels + `std::list` FIFO + `std::unordered_map` id index).

**Authorship:** ArrayBook is Robbie's. ReferenceBook, the bench harness (`bench/`), the
differential and support tests, and `bench/run.sh` were agent-written (Claude) and reviewed
by Robbie.

## Reproduce

```sh
bench/run.sh batch   # appends 3 runs to bench/results/<date>-m1pro.csv
bench/run.sh timer   # 1 run: steady_clock cost + resolution
bench/run.sh block   # 3 runs: p50/p99/p99.9 per op from 64-op blocks
bench/run.sh mixed   # 3 runs: orders/s on a steady-state mixed stream
```

`run.sh` refuses to run with uncommitted changes in `src/ include/ bench/`, stamps each row
with `git rev-parse --short HEAD`, and writes `<date>-<machine>.env` (CPU, OS, compiler).

## Machine

Apple M1 Pro, macOS 26.7.1, Apple clang 21.0.0 (clang-2100.3.34.2), Release preset
(`-O3 -DNDEBUG`, no `-march`). Runs taken on AC power with other apps closed. No core pinning: macOS has no user-space affinity API.

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

## Method: timer floor (`method=timer`)

10^6 back-to-back `steady_clock::now()` pairs: `mean_read` = loop wall / 2*10^6,
`resolution` = smallest nonzero delta. Not subtracted from results. Each 64-op block pays
one read, i.e. about `mean_read` / 64 ns/op of overhead.

## Method: block (`method=block`, `block=64`)

`steady_clock` ticks at 41.67 ns, so a single ArrayBook op (a few ns) can't be resolved:
timing one add gives p50 = 0. Instead, same workload and phases as batch, but each phase is
timed in **blocks of K = 64 consecutive ops of one kind** (one `now()` pair per block); the
trailing partial block runs untimed. Both books use the same K.

- Reported values are percentiles (nearest rank) of per-block ns / 64, i.e.
  "ns/op, per 64-op block". Quantization: +/-41.67 ns per block = +/-0.65 ns/op.
- Samples pooled across the 31 timed trials (warmup discarded): n = 10000 gives 156
  blocks/trial, ~4.8k samples per book x op; p99.9 rests on ~5 samples.
- Tails include macOS scheduler noise (no pinning) and, for the map book, system malloc
  (one `std::list` node + one `unordered_map` node per add, a `std::map` node per new
  level).
- Secondary, `method=block_coarse`, `block=1`: map book only, one op per `now()` pair.
  Its ops span 1-3 timer ticks, so these values are coarse (multiples of ~41 ns). Not
  emitted for ArrayBook: it can't be resolved per op.

## Method: mixed (`method=mixed`)

Steady-state throughput measured directly, not derived from per-op medians.

- Preload n = 10000 resting orders (untimed), then 30000 interleaved ops on live handles,
  timed as one `steady_clock` pair; ops/s = 30000 / elapsed.
- Requested mix 33 / 33 / 34 % add / modify(px) / cancel. An add is forced when live
  <= 0.95n and a cancel when live >= 1.05n, so the book stays ~10k orders (fixed cache
  footprint). Requested percents and realised counts are both in the CSV
  (`book=workload`).
- Adds and price modifies are non-crossing and same-side, as in batch. Modify keeps qty.
- Same fresh-book / interleave / warmup / 31-trial / sink discipline as batch;
  `book=ratio` = ArrayBook median ops/s / ReferenceBook median ops/s.
- Cross-check (fill in after runs): derived ops/s = 1e9 / sum(share_i x ns_i) using the
  batch medians for add, modify_px, cancel and the realised shares. Direct and derived
  should agree within ~20%; a larger gap needs a written explanation (interleaving defeats
  the batch phases' locality and branch predictability).

## CSV columns

`date,machine,compiler,commit,run,book,op,stat,value,unit,n,trials,seed,method,block`

## Results

Pending: timed runs not yet recorded.
