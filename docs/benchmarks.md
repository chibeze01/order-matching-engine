# Benchmarks

Methodology and results for the matching-engine microbenchmarks. Harnesses live
in `bench/` and use Google Benchmark. Numbers land in M2 and M5; this file is the
template they get recorded against.

## Status

Stub. Real numbers arrive with the benchmark work in M2 (first pass) and M5
(final). Do not quote the placeholder values below.

## Machine specification

Record the machine every result set was measured on.

| Field         | Value                       |
| ------------- | --------------------------- |
| CPU           | TODO (model, base/boost GHz)|
| Cores/threads | TODO                        |
| Memory        | TODO (size, type, speed)    |
| OS / kernel   | TODO                        |
| Compiler      | TODO (name and version)     |
| Build flags   | release preset, native TODO |
| Date measured | TODO                        |

## Methodology

- Build with the `release` preset. Note whether `OME_ENABLE_NATIVE` was on.
- Pin the process and disable frequency scaling where possible.
- Report median over N repetitions, plus a spread measure. State N here.
- Warm up before measuring so caches and predictors are steady.

## Results

| Benchmark      | Metric        | Value | Notes |
| -------------- | ------------- | ----- | ----- |
| order add      | ns/op         | TODO  |       |
| cancel         | ns/op         | TODO  |       |
| match (single) | ns/op         | TODO  |       |
| throughput     | messages/sec  | TODO  |       |
