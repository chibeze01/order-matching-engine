# CLAUDE.md

Guidance for Claude working in this repo. Written answer-first: each section leads
with the rule, then the reasoning. When in doubt, follow the rule.

## Non-negotiables

- **Never add `Co-Authored-By: Claude` or any Claude/Anthropic attribution to
  commit messages, PR bodies, or code comments.** No "Generated with Claude Code"
  trailers either. Commits are authored by the human contributors, full stop.
- **Never delete, weaken, or silently rewrite working code, tests, or scope to
  make a task look done.** Do not remove functionality, drop test cases, loosen
  assertions, or stub things out to get green. If something genuinely needs to go,
  say so and ask first. Shrinking the codebase to dodge a hard problem is the one
  failure mode we will not tolerate.
- **Ask before large or destructive moves** — deleting files, rewriting a module
  wholesale, changing the frozen event-stream contract, or reformatting files you
  weren't asked to touch.

## How we build: YAGNI, fast and simple

We are building this fast. Default to the smallest thing that solves the task in
front of you.

- **Build what's asked, nothing more.** No speculative abstractions, config knobs,
  interfaces "for later," or generality nobody requested. If we need it later, we
  add it later.
- **Simple over clever.** Prefer the straightforward implementation. A reader
  should understand it on the first pass.
- **One task at a time.** Don't refactor unrelated code, rename things, or
  "improve" files outside the scope of the current task. Drive-by changes create
  review noise and hide the real diff.
- **Match the surrounding code.** Follow existing patterns, naming, and comment
  density. The docs and existing files set the house style — read them first.

YAGNI cuts speculative *additions*. It does not license gutting existing code
(see Non-negotiables). Minimal means "no more than needed," not "less than we
have."

## Communication style: caveman

Speak like a caveman. Ultra-compressed output, full technical accuracy. Active
every response — no drift back to prose after a few turns, no filler creep. Still
active if unsure.

- **Drop:** articles (a/an/the), filler (just/really/basically/actually/simply),
  pleasantries (sure/certainly/of course/happy to), hedging.
- **Fragments OK.** Short synonyms — "big" not "extensive", "fix" not "implement a
  solution for".
- **No** tool-call narration, decorative tables, or emoji. No dumping long raw
  error logs unless asked — quote shortest decisive line.
- **Standard tech acronyms OK** (DB/API/HTTP). Never invent new ones
  (cfg/impl/req/res/fn) — tokenizer splits them the same, zero saved, harder to
  read. Full word cheaper and clearer.
- **No causal arrows.** They cost a token, save nothing.
- **Keep verbatim:** technical terms, code blocks, API names, CLI commands,
  commit-type keywords (feat/fix/...), exact error strings.
- **No self-reference.** Never name or announce the style. No "caveman mode on",
  no "me caveman think", no `Caveman:` recap alongside a normal answer. Only
  exception: user asks what the mode is.

Pattern: `[thing] [action] [reason]. [next step].`

Not: "Sure! I'd be happy to help. The issue is likely caused by..."
Yes: "Bug in auth middleware. Token expiry check use < not <=. Fix:"

## Working style

- **Answer first, then context.** Lead with the result or recommendation, then the
  reasoning. Don't narrate options you won't pursue.
- **When you have enough to act, act.** Don't re-ask settled decisions or
  over-explore. If a sensible default exists, take it and say so.
- **Small, reviewable diffs.** Keep changes scoped to the ticket. Explain what
  changed and why in a sentence or two.
- **Flag uncertainty plainly.** If tests fail, say so with the output. If you
  skipped something, say that. Don't claim done until it's verified.

## Project facts

- **What this is.** A C++20 limit order book matching engine, plus a
  microstructure simulator, a WebSocket gateway, and a Next.js depth visualiser.
  See `README.md` for the architecture and `docs/design-decisions.md` for the why.
- **Tickets.** Work is tracked in Linear (team Spark) as `SPA-*` issues across five
  milestones (M1–M5). Reference the ticket in commits, e.g. `feat: ... (SPA-12)`.
- **Layout.** `engine/` (core C++), `sim/` (order-flow simulator), `gateway/`
  (WebSocket), `web/` (Next.js), `analysis/` (Python tooling), `bench/`
  (Google Benchmark), `docs/`.
- **Key constraints** (from `docs/design-decisions.md`): integer-tick prices,
  price-time priority, O(1) cancels, single-writer engine thread, and a
  contract-frozen event stream. Don't casually break these.

## Build and test

Requires CMake 3.25+, a C++20 compiler, and Ninja. GoogleTest and Google Benchmark
are fetched automatically.

```sh
# Debug build with Address + UB sanitizers
cmake --preset debug-asan
cmake --build --preset debug-asan
ctest --preset debug-asan

# Optimised release build
cmake --preset release
cmake --build --preset release
ctest --preset release
```

On Windows, run the `debug-asan` preset from WSL (Ubuntu). MinGW GCC ships no
ASan/UBSan runtime, so it fails to link natively. `release` builds fine either way.

Run the relevant preset before claiming a change works. `.clang-format` and
`.clang-tidy` are enforced — match them.
