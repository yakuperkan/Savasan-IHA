# Phase 6+ Backlog (Deferred)

Bu dosya, acil olmayan ve sonraki faza ertelenen isleri tutar.

## 1) Performance Optimization (Deferred)

Targets:
- CSI FPS: `45-70` -> `60+` stable
- USB FPS: `23-27` -> `30+` stable
- Latency: `~80ms` -> `<60ms`
- GPU usage: `70-80%` -> `<70%`

Acceptance criteria:
- Repeatable benchmark script output (same camera/model/profile).
- Baseline vs latest comparison report.
- No regression on integration test suite.

## 2) Structured JSON Logging (Deferred)

Scope:
- Optional JSON log mode (env toggle).
- Stable fields: timestamp, level, tag, message, context.
- Compatibility with log analysis tooling.

Acceptance criteria:
- Text mode remains default and backward-compatible.
- JSON mode output validated with sample parser.

## 3) Real-time Dashboard (Deferred)

Scope:
- Lightweight metrics endpoint and dashboard.
- FPS/latency/GPU/health historical trend.
- Alert hints (warning/critical thresholds).

Acceptance criteria:
- Live view update interval <= 2s.
- Last 24h metrics visible.

## 4) Advanced Features (Deferred)

Scope:
- Multi-camera support (CSI + USB).
- Web interface for runtime controls.
- Advanced autopilot features (mission/waypoint/multi-target).

Acceptance criteria:
- Feature flags for safe rollout.
- Integration tests for each enabled feature path.
