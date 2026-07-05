# Event stream

Event stream v1 wire format, frozen 31 Jul (SPA-10).

This document defines the ITCH-style binary event stream emitted by the matching
engine: message types, field layouts, and endianness. It is the contract shared
by the binary event log, the WebSocket gateway, the Python analysis tooling, and
the Next.js visualiser, so once it is frozen it does not change without a version
bump.

## Status

Stub. The wire format is designed and frozen under SPA-10 in M2. Until then the
sections below are placeholders.

## Message types

TODO(SPA-10): add order, cancel, and trade messages.

## Field layout

TODO(SPA-10): document field offsets, widths, and endianness.

## Versioning

TODO(SPA-10): describe how the version byte works and the compatibility policy.
