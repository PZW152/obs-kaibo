// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <functional>

class QObject;

namespace kaibo {

// One background thread for every network call, so the OBS UI thread never blocks
// and the (non-thread-safe) Bilibili client is only ever touched from one place.
namespace worker {

void start();
// Stops accepting work and lets the thread exit after the request in flight;
// never blocks OBS shutdown on a slow network.
void shutdown();

// Runs `work` on the worker thread, then `done` on the thread that owns `context`
// (the UI thread). `done` is dropped if `context` was destroyed or the module is
// unloading, so it may touch widgets freely. `work` must not touch UI objects.
void run(QObject *context, std::function<void()> work, std::function<void()> done = {});

// Delivers `fn` to the UI thread from any thread (OBS signal handlers, hotkeys).
void postToUi(QObject *context, std::function<void()> fn);

} // namespace worker
} // namespace kaibo
