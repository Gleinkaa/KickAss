# AGENTS.md — `KickAss`

Guidance for AI agents working in this repo (VST3 kick synth, research). See `README.md`.

## Audio format — NEVER MP3

**Never produce, convert to, or process MP3.** Nik refuses to work with MP3 in
any form. This rule applies to all audio repos, not just this one.

- Deliver **WAV (PCM)**; `PCM_24` for renders and deliverables.
- **FLAC** only if size genuinely forces a compressed container.
- MP3 is never acceptable — not for previews, not for "a quick listen", not for
  Telegram delivery, not for comparison bounces.

This work is production material that gets mastered, re-pitched, warped and
stem-split further. Lossy encoding is destructive at every one of those steps
and is unrecoverable. For size-limited transport, do not transcode: use FLAC,
split the WAV, or put it on Drive and send a link.

Stated 2026-09-15 after an agent exported comparison mixes to MP3 unprompted:
*"MP3 is shit, i will never work or process anything with mp3 — NEVER"*.

## Git

- **No auto-commit, no auto-push.** Explicit approval for every commit and push.
- Remotes are SSH-only (`git@github.com:Gleinkaa/...`).
