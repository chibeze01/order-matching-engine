# Order Matching Engine

A C++ limit order book matching engine with market microstructure simulation and a real-time depth visualiser.

[![CI](https://github.com/chibeze01/order-matching-engine/actions/workflows/ci.yml/badge.svg)](https://github.com/chibeze01/order-matching-engine/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

> Demo GIF coming soon. A recording of the live depth visualiser driven by simulated order flow lands in M4.

## Architecture

```
                ┌─────────────────────────────┐
                │    Order flow simulator     │
                │   ZI · Hawkes · MM agents   │
                └──────────────┬──────────────┘
                               ▼
                ┌─────────────────────────────┐
                │    Matching engine (C++)    │
                │ price-time LOB · O(1) cancel│
                └──────────────┬──────────────┘
                               │  ITCH-style event stream
                ┌──────────────┴──────────────┐
                ▼                             ▼
     ┌────────────────────┐        ┌────────────────────┐
     │  Binary event log  │        │  WebSocket gateway │
     └─────────┬──────────┘        └─────────┬──────────┘
               ▼                             ▼
     ┌────────────────────┐        ┌────────────────────┐
     │  Python analysis   │        │ Next.js visualiser │
     └────────────────────┘        └────────────────────┘
```

The simulator drives synthetic order flow into the matching engine. The engine
maintains a price-time limit order book and emits an ITCH-style event stream.
That single stream fans out two ways: to a binary log that the Python tooling
replays for stylised-facts analysis, and to a WebSocket gateway that feeds the
Next.js visualiser in real time.

## Design decisions

See [docs/design-decisions.md](docs/design-decisions.md) for the full reasoning.

- **Integer-tick prices.** Prices are integer tick counts, not floats, so
  comparisons and price-time ordering stay exact.
- **Price-time priority.** Match best price first, then arrival order within a
  level, matching how real continuous auctions work.
- **O(1) cancels.** Real flow is cancel-dominated, so cancels sit on the hot
  path. Every resting order holds a handle to its node and unlinks in constant
  time.
- **Single-writer engine thread.** One thread owns the book, which keeps the hot
  path lock-free and the ordering deterministic.
- **Contract-frozen event stream.** The wire format is frozen once (SPA-10) and
  every consumer reads the same bytes.

## Roadmap

The work is organised into five milestones, tracked in Linear (team Spark,
project "Order Matching Engine (C++)").

| Milestone | Theme                          | What lands                                                        |
| --------- | ------------------------------ | ----------------------------------------------------------------- |
| M1        | Foundations and book core      | Repo scaffold, CI, build system, core types, the limit order book |
| M2        | Matching engine and event stream | Price-time matching, ITCH-style event stream, binary event log  |
| M3        | Microstructure simulator       | Zero-intelligence and Hawkes order flow, market-maker agents      |
| M4        | Real-time visualiser           | WebSocket gateway and the Next.js depth visualiser                |
| M5        | Benchmarks, docs and demo      | Google Benchmark harnesses, final docs, and the demo recording    |

## Building and testing

Requires CMake 3.25 or newer, a C++20 compiler, and Ninja. GoogleTest and Google
Benchmark are fetched automatically.

```sh
# Debug build with Address and UB sanitizers
cmake --preset debug-asan
cmake --build --preset debug-asan
ctest --preset debug-asan

# Optimised release build
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Native tuning (`-march=native`) is off by default so CI stays portable. Turn it
on for local release builds with `-DOME_ENABLE_NATIVE=ON`.

## Benchmarks

Methodology lives in [docs/benchmarks.md](docs/benchmarks.md). Numbers land in
M2 and M5.

## Contributors

- CJ Nwangwu ([@chibeze01](https://github.com/chibeze01))
- Second engineer (handle TBD)
