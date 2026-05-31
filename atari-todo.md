# Atari Port TODO

Tracked follow-ups for the Atari ST/MiNT port.

## Rendering / Present Path

- [ ] Re-enable screen shake on Atari ST (`Shake_The_Screen` in `TIBERIANDAWN/CONQUER.CPP`).
  - Current status: disabled via early return under `#ifdef ATARI_ST` (May 2026).
  - Reason: shake offset blits (`HidPage` → `SeenBuff` with 2-pixel vertical shift) miss all planar fast paths in `Linear_Blit_To_Linear` and fall through to per-pixel `ST_Planar_GetPixel` / `ST_Planar_PutPixel` (~320×198 pixels per shake frame).
  - Fix options: call `ST_Blitter_Planar_Rect_Blit` for cross-buffer planar rect copies (without requiring `src_gb == dest_gb`), or add a dedicated hidden→visible shake blit helper.
  - Primary files: `TIBERIANDAWN/CONQUER.CPP`, `TIBERIANDAWN/ATARILIB/drawbuff.cpp`.

- [ ] Restore a WIN32-compatible present contract so `GScreenClass::Render` does not need an explicit `Blit_Display()` call to make frames visible.
  - Context: currently documented inline in `TIBERIANDAWN/GSCREEN.CPP`.
  - Goal: backend guarantees `render -> present` behavior consistently (like original WIN32 assumptions), while preserving mouse/UI composition order.

- [ ] Revisit ST 320x200 double-buffer semantics (`HidPage` vs `SeenBuff`) and decide on one canonical strategy:
  - explicit per-frame blit in engine loop, or
  - backend-owned present/flip.
  - Primary files: `TIBERIANDAWN/GSCREEN.CPP`, `TIBERIANDAWN/ATARILIB/startup.cpp`, `TIBERIANDAWN/ATARILIB/gbuffer.cpp`.

## KeyFrame / Shape Decode Robustness

- [ ] Finish hardening XOR-chain `Build_Frame` behavior when subframe table windows hit asset end.
  - Current status: endian-safe reload fix landed; decode is improved but still logs table-end cases for some assets.
  - Primary file: `TIBERIANDAWN/KEYFRAME.CPP`.
  - Desired outcome: avoid visual holes/artifacting from failed chain steps while preserving correctness.

## Palette / Fade Behavior

- [ ] Implement timed palette fades for Atari (not just immediate set-to-target).
  - Current status: `Fade_Palette_To()` applies target palette immediately as a compatibility stopgap.
  - Primary file: `TIBERIANDAWN/ATARILIB/palette.cpp`.
  - Desired outcome: smoother transitions matching original behavior where feasible.

## Startup / Platform Services

- [ ] Implement/verify Atari `TimerClass` integration.
  - Existing marker: `TODO: Implement TimerClass for Atari ST`.
  - Primary file: `TIBERIANDAWN/ATARILIB/startup.cpp`.

- [ ] Implement Atari audio init path (`Audio_Init`) and confirm YM2149/DMA strategy.
  - Existing marker: `TODO: Implement Audio_Init for Atari ST (YM2149/DMA sound)`.
  - Primary file: `TIBERIANDAWN/ATARILIB/startup.cpp`.

- [ ] Finalize Atari video-mode handling (`Set_Video_Mode`) and window/focus behavior stubs.
  - Existing markers:
    - `TODO: Implement Set_Video_Mode for Atari ST (VDI/XBIOS)`
    - `TODO: Implement actual window switching for Atari ST`
  - Primary file: `TIBERIANDAWN/ATARILIB/startup.cpp`.

## Content / Media Platform Gaps

- [ ] Port CD detection/indexing path for Atari.
  - Existing marker: `TODO: Port Get_CD_Index to Atari ST/MiNT`.
  - Current behavior: dummy return value.
  - Primary file: `TIBERIANDAWN/CONQUER.CPP`.

- [ ] Create a `remix` content tool to repack `.MIX` archives with alignment padding.
  - Goal: ensure embedded file payloads never start on odd addresses, to avoid unaligned `short/long` accesses on m68k when data is interpreted as structs.
  - Scope idea: rewrite MIX index/offsets and inject per-entry padding while preserving file content bytes and load compatibility.

## WSA / Animation Delta APIs

- [ ] Replace remaining Atari stub implementations in `ATARILIB/wsa.cpp` with production behavior.
  - Functions currently marked/implemented as stubs include animation open/close/frame paths and page/viewport delta helpers.
  - Primary file: `TIBERIANDAWN/ATARILIB/wsa.cpp`.

## Low Priority Cleanup

- [ ] Review legacy `PG_TO_FIX` / old ST marker comments and convert still-relevant ones into explicit TODOs with owners or remove obsolete ones.
  - Example files: `TIBERIANDAWN/CONQUER.CPP`, `TIBERIANDAWN/WINSTUB.CPP`.
