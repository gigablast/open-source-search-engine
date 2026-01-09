# macOS kqueue Migration Plan

Tracking checklist for replacing the Linux-specific signal/select loop with a kqueue-backed implementation on macOS while keeping the public `Loop` API intact. Each item should be checked once the code or documentation change is complete.

## Prep Work

- [x] Confirm platform detection (`__APPLE__`, `CMAKE_SYSTEM_NAME`, etc.) so the kqueue path only compiles on Darwin targets. (`__APPLE__` is already used throughout `src/*.cpp`/`src/*.h`, and CMake exposes the `APPLE` variable; both can gate the macOS-specific code.)
- [x] Document current Loop/UdpServer responsibilities (fd readiness, timers, thread notifications, admin signals) to verify nothing is lost in the port. (`Loop` currently handles fd readiness via `select()` and `fd_set`s, timers/quickpoll through `setitimer` + sleep callbacks, thread/admin notifications via SIGCHLD/SIG* handlers, and `UdpServer` depends on `g_loop` for dispatching `Msg*` handlers in `src/UdpServer.cpp`.)

## Core Loop Changes

- [x] Add `m_kq` (kqueue descriptor) and any supporting structures (e.g., per-fd bookkeeping, timer heaps) inside `Loop` behind `#ifdef __APPLE__` guards. (`src/Loop.h` now declares `m_kq` plus per-fd enable flags and helpers guarded by `__APPLE__`; `src/Loop.cpp` defines the helper functions.)
- [x] Update `Loop::constructor`/`reset` to create/close `m_kq` on macOS and to initialize new data structures. (`Loop::Loop` zeroes the kqueue state and `Loop::reset`/`Loop::~Loop` close/clear it.)
- [x] Replace `setNonBlocking` behavior on macOS so it only sets `O_NONBLOCK` and skips `O_ASYNC`/signal wiring. (`Loop::setNonBlocking` now conditionally drops `O_ASYNC` on Darwin.)
- [x] Modify `registerReadCallback`/`registerWriteCallback` to submit EV_ADD kevents (`EVFILT_READ/EVFILT_WRITE`) with `udata` pointing at the `Slot`, and add matching EV_DELETE calls in the unregister functions. (Both register methods now call `registerKqueueEvent` after inserting the slot; `unregisterCallback` invokes `unregisterKqueueEvent` when the final watcher goes away.)
- [x] Implement a macOS-only `doPoll()` body that:
  - [x] Calls `kevent(m_kq, ...)` with an appropriate timeout (mirroring quickpoll behavior),
  - [x] Distinguishes niceness levels (invoke only niceness 0 callbacks while `m_inQuickPoll`),
  - [x] Dispatches callbacks by unpacking `udata` and reusing `callCallbacks_ass`. (The new `#ifdef __APPLE__` block in `Loop::doPoll` performs the kevent wait, splits read/write events, and reuses the existing callback machinery.)
- [x] Ensure the kevent loop respects `g_someAreQueued`/`g_udpServer.needBottom()` semantics, calling into UdpServer before sleeping as the current code does. (The mac path mirrors the pre-loop bookkeeping before blocking on `kevent`.)

## Timers & Quickpoll

- [x] Replace `setitimer`-based heartbeat/quickpoll logic with kqueue timers: register EVFILT_TIMER events for quickpoll interval and CPU accounting updates, pointing `udata` to dedicated handler routines. (Mac builds now add two kqueue timers—`KQ_TIMER_QUICK` at 10 ms and `KQ_TIMER_REAL` at 1 ms—and `Loop::doPoll` dispatches them through the refactored `quickpollTimerCore`/`realTimerCore` helpers.)
- [x] Rework `registerSleepCallback` so macOS either:
  - [x] Schedules an individual EVFILT_TIMER per sleep callback using `s->m_tick`, or
  - [x] Keeps the existing MAX_NUM_FDS mechanism but drives it with a single periodic timer event. (We chose the single periodic option: the quickpoll timer now guarantees `doPoll` wakes at least every 10 ms, so the existing `s_lastTime`/`m_minTick` logic still fires sleep callbacks at the requested cadence without additional per-callback timers.)
- [x] Update `Loop::disableTimer`/`enableTimer` so the macOS path arms/disarms the relevant timers instead of calling `setitimer`. (`Loop::disableTimer` now deletes the kqueue timers and `enableTimer` re-adds them if needed; Linux retains the `setitimer` calls.)

## Cross-thread and Admin Signals

- [x] Provide a replacement for the SIGCHLD/sigqueue wakeups from `Threads::startUp`:
  - [x] Either register an `EVFILT_USER` event and have threads call `kevent(NOTE_TRIGGER)` when `g_threads.m_needsCleanup` flips, or
  - [x] Use a self-pipe/eventfd watched via kqueue; document the chosen approach. (Chose `EVFILT_USER`: `Loop::init` registers `KQ_EVENT_WAKE`, `Loop::wakeup()` triggers NOTE_TRIGGER, and the mac path in `Threads::startUp` now calls it instead of sending SIGCHLD.)
- [x] Re-register SIGHUP/SIGTERM/SIGINT handlers with `EVFILT_SIGNAL` if macOS supports it, so `m_shutdown` still flips when those signals arrive. (`Loop::init` adds EVFILT_SIGNAL entries for those signals and the kevent loop invokes the same shutdown logic the old signal handlers used.)
- [x] Audit calls to `Loop::interruptsOn/Off` and `g_isHot`; provide no-ops or mutex-based guards on macOS where signal masking is irrelevant. (On macOS the interrupt toggles now short-circuit without touching `sigprocmask`, since kevent is used instead of async signals.)

## UdpServer & Dependent Subsystems

- [x] Confirm no `UdpServer` code path assumes `_isHot`/SIGRT semantics (e.g., `sendPoll_ass`, `makeCallbacks_ass`). If so, provide macOS alternatives or gate the logic by `#ifndef __APPLE__`. (`UdpServer` only checks `g_isHot` to decide whether to enter “real-time” mode; we now default `g_isHot` to false on macOS so those code paths fall back to the non-signal behavior.)
- [x] Verify that `g_loop.registerSleepCallback` usage in other subsystems (PingServer, Process heartbeat, etc.) still behaves with the new timer implementation. (Sleep callbacks still run off the shared periodic timer, and the kevent loop triggers them via the existing `s_lastTime`/`m_minTick` logic, so no subsystem changes were required.)

## Testing & Validation

- [ ] Build and run `./gb` on macOS with the kqueue backend; ensure startup succeeds and admin UI is reachable.
- [ ] Add a URL to spider; confirm the spider queue advances and `UdpServer` handlers fire by watching logs.
- [ ] Trigger `Save & Exit` from the admin console: verify the process shuts down cleanly.
- [ ] Send SIGINT (Ctrl-C) on the console: confirm `m_shutdown` path executes and the process exits.
- [ ] Exercise any background tasks that rely on timers (ping server, heartbeat, log flushing) to ensure the new events fire at the expected cadence.

## Stretch Goals / Cleanup

- [ ] Consider adding an epoll backend for Linux (optional) so both platforms share the same evented design, leaving the signal/select code as a fallback only.
- [ ] Update developer docs (README or `html/developer.html`) to mention macOS support status and the new event loop architecture.

## Linux epoll Backend Plan

Tracking tasks to migrate the Linux build off the legacy select/signal loop to an epoll-based backend that mirrors the macOS kqueue implementation.

- [ ] Audit the existing select/signal code path to identify all touch points (fd registration, timers, SIG handlers, cross-thread wakeups) that need epoll equivalents.
- [ ] Introduce an epoll descriptor (`m_epollFd`) and supporting data structures in `Loop` under a `#ifdef __linux__` guard, similar to the `m_kq` additions.
- [ ] Change `setNonBlocking` for Linux to skip `O_ASYNC` and rely purely on non-blocking sockets, matching the epoll model.
- [ ] Update `registerReadCallback`/`registerWriteCallback` and the unregister logic to add/remove `EPOLLIN`/`EPOLLOUT` events with edge-triggering, storing the `Slot` pointer in `epoll_event.data`.
- [ ] Implement the Linux `doPoll()` branch using `epoll_wait`, dispatching niceness level callbacks the same way the kqueue path does, and honoring `g_udpServer.needBottom()`/`g_someAreQueued` before sleeping.
- [ ] Replace the quickpoll/CPU timers with `timerfd` or `eventfd` sources monitored by epoll, so we can drop `setitimer` for Linux as well.
- [ ] Rework cross-thread wakeups (currently via sigqueue/SIGCHLD) to use an `eventfd` or pipe watched by epoll, mirroring the kqueue `EVFILT_USER` solution.
- [ ] Register SIGHUP/SIGTERM/SIGINT via `signalfd` or a small signal-handling shim that writes to the epoll wakeup fd, so the main loop no longer relies on signal handlers interrupting `select`.
- [ ] Ensure `Loop::interruptsOn/Off` become no-ops on Linux once the signal dependency is removed, just as we did on macOS.
- [ ] Test the epoll backend on a Linux build: verify spidering, HTTPS fetches, admin UI, Ctrl-C handling, and any timer-driven tasks operate correctly; keep the old select path behind a build flag for fallback.
