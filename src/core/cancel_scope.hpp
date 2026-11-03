// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <atomic>

namespace kaibo {

// A cancellation flag that only applies to the thread that registered with it.
//
// During OBS exit the worker thread's in-flight request must abort, but the synchronous
// request the UI thread still makes (the final room close) must not: a process-wide flag
// used to abort both, which could leave the room live because the close itself was
// cancelled. The transport asks requested() from whichever thread performs the request,
// so ownership is scoped per thread without any shared mutable thread id.
class CancelScope {
public:
	CancelScope() = default;
	CancelScope(const CancelScope &) = delete;
	CancelScope &operator=(const CancelScope &) = delete;

	// Registers the calling thread as the owner of this scope; call once at thread start.
	void attachToCurrentThread() { t_owner = this; }
	// Any thread may ask for cancellation; only the owner thread observes it.
	void request() { m_requested.store(true, std::memory_order_relaxed); }
	void reset() { m_requested.store(false, std::memory_order_relaxed); }
	// True only inside the thread that called attachToCurrentThread() and only after
	// request(); other threads (e.g. a synchronous close on the UI thread) stay usable.
	bool requested() const { return t_owner == this && m_requested.load(std::memory_order_relaxed); }

private:
	inline static thread_local const CancelScope *t_owner = nullptr;
	std::atomic<bool> m_requested{false};
};

} // namespace kaibo
