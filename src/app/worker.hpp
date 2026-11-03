// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <functional>

class QObject;

namespace kaibo {

// One background thread for every network call, so the OBS UI thread never blocks
// and the (non-thread-safe) Bilibili client is only ever touched from one place.
namespace worker {

void start();
// Refuses new work, aborts the request in flight, then joins the thread. Call from the
// UI thread before the objects the jobs use (client, transport, OBS frontend) go away:
// after this returns no job can still be running or post anything. Idempotent.
void shutdown();

// True once shutdown() has begun. Jobs use it to stop early and never post to the UI;
// it is process-wide state, unlike cancelled().
bool shuttingDown();
// Asks the transport to abort the request currently running on the worker thread
// (waitIdle() below stays useful); used on OBS exit, where the controller must still
// close the room synchronously.
void abortInFlight();
// True when the in-flight request should give up: shutdown() or abortInFlight() was
// called. Deliberately scoped to the worker thread: a synchronous request made by the
// UI thread during exit (the final room close) must not be aborted. The HTTP transport
// checks this from libcurl's progress callback.
bool cancelled();
// Waits until no job is running and none is queued; false when the timeout expired.
bool waitIdle(int timeoutMs);

// Runs `work` on the worker thread, then `done` on the thread that owns `context`
// (the UI thread). `done` is dropped if `context` was destroyed or the module is
// unloading, so it may touch widgets freely. `work` must not touch UI objects.
void run(QObject *context, std::function<void()> work, std::function<void()> done = {});

// Delivers `fn` to the UI thread from any thread (OBS signal handlers, hotkeys).
void postToUi(QObject *context, std::function<void()> fn);

} // namespace worker
} // namespace kaibo
