#include <algorithm>
#include <array>
#include <array_book.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
#include <reference_book.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "stats.hpp"
#include "timing.hpp"
#include "types.hpp"
#include "workload.hpp"

namespace {
  using Clock = std::chrono::steady_clock;
  using bench::kCap;
  using bench::Workload;

  // Defeats dead-code elimination: every returned id and post-phase top of book lands here.
  volatile std::int64_t sink{};

  enum class Mode : std::uint8_t { kBatch, kBlock, kTimerFloor };

  struct Config {
    Mode mode{Mode::kBatch};
    std::size_t n{10000};
    std::size_t trials{31};
    std::uint64_t seed{42};
    std::size_t block{64};
    unsigned run{1};
    std::string commit{"unknown"};
    std::string machine{"unknown"};
  };

  struct Row {
    std::string book, op, stat;
    double value;
    std::string unit, method, block;
  };

  constexpr std::string_view kHeader{
      "date,machine,compiler,commit,run,book,op,stat,value,unit,n,trials,seed,method,block"};

  std::string today() {
    const std::time_t t{std::time(nullptr)};
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &t);
#else
    gmtime_r(&t, &utc);
#endif
    std::string buf(11, '\0');
    buf.resize(std::strftime(buf.data(), buf.size(), "%F", &utc));
    return buf;
  }

  std::string compiler() {
    std::string v{__VERSION__};
    std::replace(v.begin(), v.end(), ',', ';');
    return v;
  }

  void emit(const Config& cfg, const std::vector<Row>& rows) {
    const std::string date{today()};
    const std::string cc{compiler()};
    for (const auto& r : rows) {
      std::cout << date << ',' << cfg.machine << ',' << cc << ',' << cfg.commit << ',' << cfg.run
                << ',' << r.book << ',' << r.op << ',' << r.stat << ',' << r.value << ',' << r.unit
                << ',' << cfg.n << ',' << cfg.trials << ',' << cfg.seed << ',' << r.method << ','
                << r.block << '\n';
    }
  }

  std::uint64_t nowNs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
            .count());
  }

  template <class Book>
  void foldTop(Book& book) {
    sink = sink + book.bestBid() + book.bestAsk();
  }

  constexpr std::size_t kPhases{4};
  constexpr std::array<std::string_view, kPhases> kPhaseNames{"add", "modify_px", "modify_qty_down",
                                                              "cancel"};

  // Runs the four phases on one fresh book. `time(phase, n, op)` must call op(0..n-1)
  // exactly once each, in order; it decides what gets timed.
  // Phases: add all -> modify px each -> modify qty-down each (qty-1 >= 1, in place) ->
  // cancel all in shuffled order.
  template <class Book, class Timer>
  void runPhases(Book& book, const Workload& w, std::vector<ob::OrderId>& ids, Timer&& time) {
    const std::size_t n{w.resting.size()};

    time(0, n, [&](std::size_t i) { ids[i] = book.add(w.resting[i]); });
    std::int64_t idSum{};
    for (std::size_t i{}; i < n; ++i) {
      idSum += ids[i];
    }
    sink = sink + idSum;
    foldTop(book);

    time(1, n, [&](std::size_t i) { book.modify(ids[i], w.modifyPx[i], w.resting[i].qty); });
    foldTop(book);

    time(2, n, [&](std::size_t i) { book.modify(ids[i], w.modifyPx[i], w.resting[i].qty - 1); });
    foldTop(book);

    time(3, n, [&](std::size_t i) { book.cancel(ids[w.cancelOrder[i]]); });
    foldTop(book);
  }

  std::unique_ptr<ArrayBook<kCap>> freshArray() {
    return std::make_unique<ArrayBook<kCap>>(bench::kBand);
  }
  std::unique_ptr<ReferenceBook> freshRef() { return std::make_unique<ReferenceBook>(kCap); }

  // ---- batch: one steady_clock pair per phase, ns/op = phase time / n ----

  using PhaseNs = std::array<double, kPhases>;

  template <class Book>
  PhaseNs batchTrial(Book& book, const Workload& w, std::vector<ob::OrderId>& ids) {
    PhaseNs out{};
    runPhases(book, w, ids, [&out](std::size_t phase, std::size_t n, auto&& op) {
      const std::uint64_t t0{nowNs()};
      for (std::size_t i{}; i < n; ++i) {
        op(i);
      }
      const std::uint64_t t1{nowNs()};
      out[phase] = static_cast<double>(t1 - t0) / static_cast<double>(n);
    });
    return out;
  }

  void addSummary(std::vector<Row>& rows, const std::string& book, std::string_view op,
                  const std::vector<double>& xs) {
    const auto [lo, hi]{std::minmax_element(xs.begin(), xs.end())};
    rows.push_back({book, std::string{op}, "median", bench::median(xs), "ns/op", "batch", ""});
    rows.push_back({book, std::string{op}, "min", *lo, "ns/op", "batch", ""});
    rows.push_back({book, std::string{op}, "max", *hi, "ns/op", "batch", ""});
  }

  std::vector<Row> runBatch(const Config& cfg) {
    const Workload w{bench::makeWorkload(cfg.seed, cfg.n)};
    std::vector<ob::OrderId> ids(cfg.n);
    std::array<std::vector<double>, kPhases> arr;
    std::array<std::vector<double>, kPhases> ref;
    for (std::size_t p{}; p < kPhases; ++p) {
      arr[p].reserve(cfg.trials);
      ref[p].reserve(cfg.trials);
    }

    // Trial 0 is warmup for each book (page faults, malloc arena for the map book).
    // Books interleave A,R,A,R so drift (thermal, background load) hits both equally.
    for (std::size_t t{}; t <= cfg.trials; ++t) {
      auto a{freshArray()};
      const PhaseNs pa{batchTrial(*a, w, ids)};
      auto r{freshRef()};
      const PhaseNs pr{batchTrial(*r, w, ids)};
      if (t == 0) {
        continue;
      }
      for (std::size_t p{}; p < kPhases; ++p) {
        arr[p].push_back(pa[p]);
        ref[p].push_back(pr[p]);
      }
    }

    std::vector<Row> rows;
    for (std::size_t p{}; p < kPhases; ++p) {
      addSummary(rows, "array", kPhaseNames[p], arr[p]);
      addSummary(rows, "ref", kPhaseNames[p], ref[p]);
      rows.push_back({"ratio", std::string{kPhaseNames[p]}, "median",
                      bench::median(ref[p]) / bench::median(arr[p]), "x", "batch", ""});
    }
    return rows;
  }

  // ---- block: one steady_clock pair per k consecutive ops of one kind ----
  // The timer ticks at 41.67 ns on Apple silicon, too coarse for single ArrayBook ops, so
  // tails are percentiles of per-block ns / k.

  using PhaseSamples = std::array<std::vector<std::uint64_t>, kPhases>;

  template <class Book>
  void blockTrial(Book& book, const Workload& w, std::vector<ob::OrderId>& ids, std::size_t k,
                  PhaseSamples& out) {
    runPhases(book, w, ids, [&out, k](std::size_t phase, std::size_t n, auto&& op) {
      bench::timeBlocks(n, k, op, nowNs, out[phase]);
    });
  }

  PhaseSamples reserved(std::size_t perPhase) {
    PhaseSamples s;
    for (auto& v : s) {
      v.reserve(perPhase);
    }
    return s;
  }

  void addTails(std::vector<Row>& rows, const std::string& book, PhaseSamples& s, std::size_t k,
                const std::string& method) {
    const std::string block{std::to_string(k)};
    const auto perOp{
        [k](std::uint64_t ns) { return static_cast<double>(ns) / static_cast<double>(k); }};
    for (std::size_t p{}; p < kPhases; ++p) {
      std::sort(s[p].begin(), s[p].end());
      const std::string op{kPhaseNames[p]};
      rows.push_back(
          {book, op, "p50", perOp(bench::percentile(s[p], 50.0)), "ns/op", method, block});
      rows.push_back(
          {book, op, "p99", perOp(bench::percentile(s[p], 99.0)), "ns/op", method, block});
      rows.push_back(
          {book, op, "p99.9", perOp(bench::percentile(s[p], 99.9)), "ns/op", method, block});
      rows.push_back(
          {book, op, "samples", static_cast<double>(s[p].size()), "blocks", method, block});
    }
  }

  std::vector<Row> runBlock(const Config& cfg) {
    const Workload w{bench::makeWorkload(cfg.seed, cfg.n)};
    std::vector<ob::OrderId> ids(cfg.n);
    const std::size_t k{cfg.block};
    // Sized for warmup + timed trials so no push_back reallocates mid-run.
    PhaseSamples arr{reserved((cfg.trials + 1) * (cfg.n / k))};
    PhaseSamples ref{reserved((cfg.trials + 1) * (cfg.n / k))};
    // Secondary: map book at k = 1. Its ops span only 1-3 timer ticks, so this is coarse.
    PhaseSamples refSingle{reserved((cfg.trials + 1) * cfg.n)};

    for (std::size_t t{}; t <= cfg.trials; ++t) {
      auto a{freshArray()};
      blockTrial(*a, w, ids, k, arr);
      auto r{freshRef()};
      blockTrial(*r, w, ids, k, ref);
      auto r1{freshRef()};
      blockTrial(*r1, w, ids, 1, refSingle);
      if (t == 0) {
        for (auto* s : {&arr, &ref, &refSingle}) {
          for (auto& v : *s) {
            v.clear();
          }
        }
      }
    }

    std::vector<Row> rows;
    addTails(rows, "array", arr, k, "block");
    addTails(rows, "ref", ref, k, "block");
    if (k != 1) {
      addTails(rows, "ref", refSingle, 1, "block_coarse");
    }
    return rows;
  }

  // ---- timer floor: cost and resolution of steady_clock::now() ----

  std::vector<Row> runTimerFloor() {
    constexpr std::size_t kPairs{1'000'000};
    std::uint64_t minDelta{UINT64_MAX};
    const std::uint64_t start{nowNs()};
    for (std::size_t i{}; i < kPairs; ++i) {
      const std::uint64_t a{nowNs()};
      const std::uint64_t b{nowNs()};
      if (b > a && b - a < minDelta) {
        minDelta = b - a;
      }
    }
    const std::uint64_t end{nowNs()};
    const double perRead{static_cast<double>(end - start) / static_cast<double>(2 * kPairs)};
    return {
        {"timer", "steady_clock_now", "mean_read", perRead, "ns", "timer", ""},
        {"timer", "steady_clock_now", "resolution", static_cast<double>(minDelta), "ns", "timer",
         ""},
    };
  }

  // ---- CLI ----

  [[noreturn]] void usage(const std::string& why) {
    std::cerr << "ob_bench: " << why << "\n"
              << "usage: ob_bench [--header] [--per-op [--block K] | --timer-floor]\n"
              << "                [--n N] [--trials T] [--seed S] [--run R]\n"
              << "                [--commit SHA] [--machine LABEL]\n"
              << "Prints CSV rows to stdout (--header prints the header line only).\n";
    std::exit(2);
  }

  std::uint64_t toU64(const std::string& s) {
    try {
      std::size_t used{};
      const auto v{std::stoull(s, &used)};
      if (used != s.size()) {
        usage("not a number: " + s);
      }
      return v;
    } catch (const std::exception&) {
      usage("not a number: " + s);
    }
  }

  Config parse(int argc, char** argv, bool& headerOnly) {
    Config cfg;
    const std::vector<std::string> args(argv + 1, argv + argc);
    for (std::size_t i{}; i < args.size(); ++i) {
      const std::string& a{args[i]};
      if (a == "--header") {
        headerOnly = true;
        continue;
      }
      if (a == "--per-op") {
        cfg.mode = Mode::kBlock;
        continue;
      }
      if (a == "--timer-floor") {
        cfg.mode = Mode::kTimerFloor;
        continue;
      }
      if (i + 1 >= args.size()) {
        usage("missing value or unknown flag: " + a);
      }
      const std::string& v{args[++i]};
      if (a == "--n") {
        cfg.n = static_cast<std::size_t>(toU64(v));
      } else if (a == "--trials") {
        cfg.trials = static_cast<std::size_t>(toU64(v));
      } else if (a == "--seed") {
        cfg.seed = toU64(v);
      } else if (a == "--block") {
        cfg.block = static_cast<std::size_t>(toU64(v));
      } else if (a == "--run") {
        cfg.run = static_cast<unsigned>(toU64(v));
      } else if (a == "--commit") {
        cfg.commit = v;
      } else if (a == "--machine") {
        cfg.machine = v;
      } else {
        usage("unknown flag: " + a);
      }
    }
    if (cfg.n == 0 || cfg.n > kCap) {
      usage("--n must be in 1.." + std::to_string(kCap) + " (ArrayBook pool size)");
    }
    if (cfg.trials == 0) {
      usage("--trials must be >= 1");
    }
    if (cfg.block == 0 || cfg.block > cfg.n) {
      usage("--block must be in 1..n");
    }
    return cfg;
  }
}  // namespace

int main(int argc, char** argv) {
  bool headerOnly{false};
  const Config cfg{parse(argc, argv, headerOnly)};
  if (headerOnly) {
    std::cout << kHeader << '\n';
    return 0;
  }
  switch (cfg.mode) {
    case Mode::kBatch:
      emit(cfg, runBatch(cfg));
      break;
    case Mode::kBlock:
      emit(cfg, runBlock(cfg));
      break;
    case Mode::kTimerFloor:
      emit(cfg, runTimerFloor());
      break;
  }
  std::cerr << "sink " << sink << '\n';
  return 0;
}
