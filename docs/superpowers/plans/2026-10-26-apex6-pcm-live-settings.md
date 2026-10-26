# APEX 6 PCM Live Settings Implementation Plan

**Goal:** Apply the resolved game PCM policy without restarting game or bridge, acknowledging actual engine application.

**Architecture:** Preserve the 76-byte measurement mapping unchanged. Add a session-token-scoped control mapping with a separate named mutex, protocol version, game identity, request revision and applied revision. Validate settings and apply at render block boundaries using a zero-timeout IPC lock. Use an immutable local scaler snapshot for each block; protect published policy statistics separately without blocking rendering.

**Constraints:** No new render-thread file I/O, blocking waits or allocations. Preserve rumble, triggers, waveform timing, clipping and gain bounds. Reuse ResolveApex6PcmPolicy. Debounce UI edits 150ms and flush final drag values. Only ACKed revisions count as applied. Session identity changes invalidate pending UI results. Hardware timing and feel remain unverified.

## Tasks
- [x] Engine: add behavioral control IPC tests; verify failure before implementation. Implement validated control protocol, game identity option and block-boundary application; make tests pass.
- [x] Tray: add behavioral client/policy tests; verify failure before implementation. Add control client, active-session validation, debounced policy dispatch and ACK status; preserve measurement/save workflows.
- [x] Integration: exercise invalid versions/ranges, stale game/session, repeated edits, contention, statistics access and reconnect. Review changed code and fix verified defects.
- [x] Verify: after the existing Tray exited, reran scripts/build-windows.ps1, all Release CTest (35/35), and scripts/build-tray-app.ps1 successfully. Normal-output WPF Release build has zero warnings/errors; managed Release regression passes 461 assertions. WPF/measurement static contracts previously passed 3/3. Hardware and actual WPF interaction remain unverified.
- [ ] Git: final diff/status checked; include only feature source/tests/docs, excluding build outputs and tool-created .serena metadata. Commit requested message after all required builds/tests passed; push origin stable without force.

Baseline stable HEAD: 010ec0b489c99985dc612b2907d064c703e283aa. Initial working tree clean. User explicitly requested work directly on stable and authorized commit/push without additional approval.
