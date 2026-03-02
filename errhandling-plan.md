# Error Handling Plan (`src/common`)

Date: 2026-03-02
Scope: error-path hardening for `src/common` with small, reviewable commits.

## Goals

1. Eliminate high-risk unchecked error paths that can lead to undefined
   behavior, silent data loss, or crashes.
2. Prefer shared helpers over ad-hoc loops and repeated patterns.
3. Keep each change narrow, testable, and easy to review.

## Non-goals / Pushback

1. Do not do a blanket "check every `close()`" sweep. That creates noisy,
   low-value churn.
2. Do not mix the large IRC partial-send refactor into small fixes. Treat it
   as a dedicated track.

## Priority Summary

P0 (high):
1. Child-pipe line reads in `server_read_child` ignore read failures.
2. IRC send path can drop bytes on partial writes (structural issue).

P1 (medium):
1. `/EXEC` child fd setup ignores `close`/`dup2` failures.
2. `waitline()` does not retry on `EINTR`.
3. `http_read_line()` still uses ad-hoc `write()` calls.
4. `copy_file()` does not handle short writes and `EINTR` cleanly.
5. Proxy URI parsing can dereference malformed strings.
6. DCC ACK sends ignore send failures.

---

## Execution Plan

### Step 1: Harden child-pipe protocol reads (`server.c`)

Problem:
- `server_read_child()` calls `waitline2()` multiple times and ignores return
  values. EOF/error can leave stale/uninitialized buffers and invalid branch
  behavior.

Change:
1. Add a small helper (for example `child_read_line(...)`) that wraps
   `waitline()`, checks return value, and reports failure.
2. Use it for every child-pipe read in `server_read_child()`.
3. On read failure, tear down the connect attempt safely and trigger existing
   reconnect behavior.

Commit shape:
- `common: Validate connect helper child pipe reads`

Validation:
1. `meson compile -C _build-gtk4`
2. `meson test -C _build-gtk4 --print-errorlogs`

---

### Step 2: Make `waitline()` EINTR-safe (`util.c`)

Problem:
- `waitline()` treats interrupted `read`/`recv` as hard failure.

Change:
1. Retry on `EINTR` in both recv/read branches.
2. Keep existing API and semantics for all other errors.

Commit shape:
- `common: Retry waitline reads on EINTR`

Validation:
1. compile + tests as above.

---

### Step 3: Harden `/EXEC` child fd plumbing (`outbound.c`)

Problem:
- Child path ignores return values from `close()` and `dup2()`.

Change:
1. Check critical `dup2()` calls.
2. If setup fails, emit `g_printerr(...)` and `_exit(127)`.
3. Keep behavior unchanged on success path.

Commit shape:
- `common: Check /exec child descriptor setup`

Validation:
1. compile + tests.
2. Manual smoke: `/exec -d true`, `/exec -d bad-command`.

---

### Step 4: Use shared exact writes in HTTP proxy line reporting (`server.c`)

Problem:
- `http_read_line()` still uses multiple ad-hoc `write()` calls.

Change:
1. Replace with `write_all()` helper.
2. Preserve exact output format and existing warning behavior.

Commit shape:
- `common: Use write_all for HTTP proxy child pipe output`

Validation:
1. compile + tests.

---

### Step 5: Harden file move copy path (`util.c`)

Problem:
- `copy_file()` uses single `write()` and can silently short-write.
- `read()` loop is not EINTR-safe.

Change:
1. Use `write_all()` for destination writes.
2. Retry source reads on `EINTR`.
3. Keep current user-visible messages and return values.

Commit shape:
- `common: Make file move copy loop exact and EINTR-safe`

Validation:
1. compile + tests.
2. Manual file move smoke for DCC completed-dir path.

---

### Step 6: Harden proxy URI parsing (`server.c`)

Problem:
- Auto-proxy parsing assumes delimiters exist and can dereference malformed
  strings (`strchr(...)+3`, unchecked host/port split).

Change:
1. Validate URI layout before pointer arithmetic.
2. Reject malformed proxy entries safely (fall back to direct/no proxy).
3. Log parse failure with a clear warning.

Commit shape:
- `common: Validate auto-proxy URI parsing`

Validation:
1. compile + tests.
2. Manual smoke with malformed proxy env/list input.

---

### Step 7: Check DCC ACK send failures (`dcc.c`)

Problem:
- `dcc_send_ack()` ignores send return values.

Change:
1. Check send result and handle failure consistently with existing DCC error
   flow.
2. Keep ACK wire format unchanged.

Commit shape:
- `common: Handle DCC ACK send errors`

Validation:
1. compile + tests.

---

## Separate Design Track (Not a tiny patch)

### Track A: Correct partial-write semantics in IRC send queue (`server.c`)

Current analysis:
1. `tcp_send_queue()` removes a queued line after one `server_send_real()`
   call, regardless of `send()`/`SSL_write()` returning a short write.
2. Retrying by slicing the original command buffer is incorrect because
   `tcp_send_real()` sends converted bytes (`text_convert_invalid()`), and
   `ret` is measured on the converted buffer length, not the original UTF-8.
3. `server_send_real()` also does `fe_add_rawlog()` + `url_check_line()`;
   retries must not duplicate those side effects.
4. Existing queue storage (`char *` with priority byte prefix) cannot track:
   sent offset, "rawlog already emitted", or "throttle already charged".
5. Fatal send errors are not handled in the write path; current behavior is
   "best effort and drop".

Behavior invariants to preserve:
1. Priority ordering stays `2 -> 1 -> 0` (same command classification rules).
2. Throttle pacing (`next_send`, `prev_now`) stays based on one logical IRC
   line, not on retry fragments.
3. A queued line emits rawlog/url side effects exactly once.
4. Disconnect/reconnect flow remains aligned with existing `server_read()`
   behavior and `server_cleanup()` ownership.
5. `sendq_len` remains a logical queue metric exposed to `/flushq` and plugin
   list APIs (decrement when a full queued command is fully drained).

Implementation series:
1. **A0: Add realistic reproducible PoC in `tests/`**
   - Add integration PoC that runs the real GTK4 client against a local fake
     IRC server.
   - Use an `LD_PRELOAD` shim to force exactly one short `send()` for a known
     outbound `PONG ...` line (realistic kernel behavior, deterministic test).
   - Initial behavior (pre-fix) demonstrates truncation under forced short
     write.
   - Final behavior (post-fix) is strict regression semantics: require full
     `PONG` delivery under the same forced short-write condition.
2. **A1: Add queue entry model (mechanical, no behavior change yet)**
   - Introduce an internal queue entry struct in `server.c`, for example:
     `priority`, `raw_buf/raw_len`, `wire_buf/wire_len`, `wire_off`,
     `throttle_charged`, `rawlog_emitted`.
   - Add small helpers: alloc/free entry, priority classification, throttle
     cost calculation.
   - Keep enqueue/dequeue behavior equivalent to current code in this commit,
     so review focuses on data model migration only.
3. **A2: Convert once at enqueue, not per retry**
   - In `tcp_send_len()`, allocate queue entry and precompute `wire_buf` using
     `text_convert_invalid(..., serv->write_converter, ...)`.
   - Keep `raw_buf` for one-shot `fe_add_rawlog()` and `url_check_line()`.
   - Replace direct queue `char *` usage with entry pointers.
4. **A3: Correct partial-write drain logic**
   - Add a send helper that writes from `wire_buf + wire_off`.
   - On `ret > 0`: advance `wire_off`; only dequeue when `wire_off == wire_len`.
   - On `ret < 0` with `EINTR`: retry same entry in-place.
   - On `ret < 0` with `EAGAIN/EWOULDBLOCK`: keep entry queued and return `1`
     so timeout callback remains active.
   - Ensure once a line starts sending, retries continue that same line before
     moving on (no byte interleaving across queued commands).
5. **A4: Throttle and side-effect correctness**
   - Emit rawlog/url once, at first actual send attempt of the entry.
   - Apply throttle charge once per logical entry (`throttle_charged` guard),
     never once per retry fragment.
   - Keep existing clock-skew guard and timeout return semantics.
6. **A5: Fatal send error policy**
   - Treat non-recoverable send failures as connection failure:
     disconnect + existing reconnect policy, mirroring read-side behavior.
   - Ensure queue cleanup happens through existing cleanup/flush paths only
     (single ownership, no double frees).
7. **A6: Cleanup and polish**
   - Replace `list_free(&serv->outbound_queue)` usage with queue-entry-aware
     free helper in `server_flush_queue()`.
   - Keep `server_free()` behavior unchanged by routing through
     `serv->flush_queue`.
8. **A7: Flip PoC into strict regression**
   - Once A3/A4 land, invert PoC assertions so success requires receiving the
     full expected `PONG` line under forced short-send conditions.
   - Keep the short-send preload in place to continuously guard against
     regressions in queue drain semantics.

Validation plan:
1. Build and tests (fuzz-tests enabled build dir): `meson compile -C _build-asan-ubsan`, `meson test -C _build-asan-ubsan --print-errorlogs`.
2. Explicit PoC run before and after fix:
   - Current regression: `meson test -C _build-asan-ubsan \"IRC Partial Send Queue Regression\" --print-errorlogs`
     should report `IRC_PARTIAL_SEND_QUEUE_REGRESSION=OK`.
3. Manual throttle smoke:
   - Send multi-line burst (`/quote` loop, `/msg` spam) and verify no dropped
     or mangled lines.
   - Validate priority behavior still prefers non-`WHO`/non-`PRIVMSG` traffic.
4. Manual partial-write forcing:
   - Use a debug-only short-send hook (temporary instrumentation) or tiny send
     buffer environment to force repeated partial writes and confirm eventual
     full drain without duplication.
5. Failure-path smoke:
   - Disconnect peer during queued outbound traffic and confirm expected
     disconnect/reconnect flow, with queue flushed exactly once.

Suggested commit titles:
1. `tests: Add IRC short-send PoC for outbound queue truncation`
2. `common: Introduce structured IRC outbound queue entries`
3. `common: Pre-encode IRC outbound queue payloads`
4. `common: Drain IRC send queue across partial writes`
5. `common: Charge throttle and rawlog once per queued line`
6. `common: Handle fatal IRC send queue write failures`
7. `tests: Flip IRC short-send PoC into strict regression`

Reason this is separate:
- This touches queue semantics and throughput behavior, so it should not be
  bundled with small error-handling commits.

---

## Suggested Delivery Order

1. A0 `tests: Add IRC short-send PoC for outbound queue truncation`
2. A1 `common: Introduce structured IRC outbound queue entries`
3. A2 `common: Pre-encode IRC outbound queue payloads`
4. A3 `common: Drain IRC send queue across partial writes`
5. A4 `common: Charge throttle and rawlog once per queued line`
6. A5 `common: Handle fatal IRC send queue write failures`
7. A6 cleanup/polish in `server_flush_queue` and `server_free` paths
8. A7 `tests: Flip IRC short-send PoC into strict regression`

## Done Criteria

1. Each Track A commit is focused and reviewable, with explicit `Problem:` /
   `Fix:` rationale in the commit message.
2. `meson compile -C _build-asan-ubsan` and
   `meson test -C _build-asan-ubsan --print-errorlogs` pass after each commit.
3. Forced short-write testing shows no dropped/truncated IRC commands and no
   duplicated rawlog/url side effects.
4. Priority and throttle behavior remain consistent with current intent
   (priority ordering unchanged, no retry-fragment over-throttling).
