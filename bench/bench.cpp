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
#include "types.hpp"
#include "workload.hpp"

namespace {
  using Clock = std::chrono::steady_clock;
  using bench::kCap;
  using bench::Workload;

  // Defeats dead-code elimination: every returned id and post-phase top of book lands here.
  volatile std::int64_t sink{};

  struct Config {
    std::size_t n{10000};
    std::size_t trials{31};
    std::uint64_t seed{42};
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
    for (auto& c : v) {
      if (c == ',') {
        c = ';';
      }
    }
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

  double nsPerOp(Clock::time_point t0, Clock::time_point t1, std::size_t n) {
    return static_cast<double>(
               std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) /
           static_cast<double>(n);
  }

  template <class Book>
  void foldTop(Book& book) {
    sink = sink + book.bestBid() + book.bestAsk();
  }

  // ---- batch mode: one steady_clock pair per phase, ns/op = phase time / n ----

  constexpr std::size_t kPhases{4};
  constexpr std::string_view kPhaseNames[kPhases]{"add", "modify_px", "modify_qty_down", "cancel"};

  struct PhaseNs {
    double v[kPhases];
  };

  template <class Book>
  PhaseNs runTrial(Book& book, const Workload& w, std::vector<ob::OrderId>& ids) {
    const std::size_t n{w.resting.size()};
    PhaseNs out{};
    std::int64_t idSum{};

    auto t0{Clock::now()};
    for (std::size_t i{}; i < n; ++i) {
      ids[i] = book.add(w.resting[i]);
    }
    auto t1{Clock::now()};
    out.v[0] = nsPerOp(t0, t1, n);
    for (const auto id : ids) {
      idSum += id;
    }
    foldTop(book);

    t0 = Clock::now();
    for (std::size_t i{}; i < n; ++i) {
      book.modify(ids[i], w.modifyPx[i], w.resting[i].qty);
    }
    t1 = Clock::now();
    out.v[1] = nsPerOp(t0, t1, n);
    foldTop(book);

    t0 = Clock::now();
    for (std::size_t i{}; i < n; ++i) {
      book.modify(ids[i], w.modifyPx[i], w.resting[i].qty - 1);
    }
    t1 = Clock::now();
    out.v[2] = nsPerOp(t0, t1, n);
    foldTop(book);

    t0 = Clock::now();
    for (const std::size_t h : w.cancelOrder) {
      book.cancel(ids[h]);
    }
    t1 = Clock::now();
    out.v[3] = nsPerOp(t0, t1, n);
    foldTop(book);

    sink = sink + idSum;
    return out;
  }

  std::unique_ptr<ArrayBook<kCap>> freshArray() {
    return std::make_unique<ArrayBook<kCap>>(bench::kBand);
  }
  std::unique_ptr<ReferenceBook> freshRef() { return std::make_unique<ReferenceBook>(kCap); }

  void addSummary(std::vector<Row>& rows, const std::string& book, std::string_view op,
                  const std::vector<double>& xs, const std::string& method) {
    double lo{xs.front()};
    double hi{xs.front()};
    for (const double x : xs) {
      lo = x < lo ? x : lo;
      hi = x > hi ? x : hi;
    }
    rows.push_back({book, std::string{op}, "median", bench::median(xs), "ns/op", method, ""});
    rows.push_back({book, std::string{op}, "min", lo, "ns/op", method, ""});
    rows.push_back({book, std::string{op}, "max", hi, "ns/op", method, ""});
  }

  std::vector<Row> runBatch(const Config& cfg) {
    const Workload w{bench::makeWorkload(cfg.seed, cfg.n)};
    std::vector<ob::OrderId> ids(cfg.n);
    std::vector<double> arr[kPhases];
    std::vector<double> ref[kPhases];
    for (std::size_t p{}; p < kPhases; ++p) {
      arr[p].reserve(cfg.trials);
      ref[p].reserve(cfg.trials);
    }

    // Trial 0 is warmup for each book (page faults, malloc arena for the map book).
    // Books interleave A,R,A,R so drift (thermal, background load) hits both equally.
    for (std::size_t t{}; t <= cfg.trials; ++t) {
      auto a{freshArray()};
      const PhaseNs pa{runTrial(*a, w, ids)};
      auto r{freshRef()};
      const PhaseNs pr{runTrial(*r, w, ids)};
      if (t == 0) {
        continue;
      }
      for (std::size_t p{}; p < kPhases; ++p) {
        arr[p].push_back(pa.v[p]);
        ref[p].push_back(pr.v[p]);
      }
    }

    std::vector<Row> rows;
    for (std::size_t p{}; p < kPhases; ++p) {
      addSummary(rows, "array", kPhaseNames[p], arr[p], "batch");
      addSummary(rows, "ref", kPhaseNames[p], ref[p], "batch");
      rows.push_back({"ratio", std::string{kPhaseNames[p]}, "median",
                      bench::median(ref[p]) / bench::median(arr[p]), "x", "batch", ""});
    }
    return rows;
  }

  // ---- CLI ----

  [[noreturn]] void usage(const std::string& why) {
    std::cerr << "ob_bench: " << why << "\n"
              << "usage: ob_bench [--header] [--n N] [--trials T] [--seed S] [--run R]\n"
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
  emit(cfg, runBatch(cfg));
  std::cerr << "sink " << sink << '\n';
  return 0;
}
