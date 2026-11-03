// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "core/settings_model.hpp"

#include <functional>
#include <string>

struct calldata;
struct obs_output;

namespace kaibo::obs {

// All functions here must be called on the OBS UI thread.

// Copies the current OBS stream service into `backup` when it is a real user service
// (not a Bilibili ingest one that Kaibo wrote). Returns true when something usable was
// captured; the caller persists it before OBS is modified.
bool captureStreamService(ServiceBackup &backup);

// Replaces OBS's stream service with a custom RTMP server/key and saves it. Call
// captureStreamService() first if the previous service should be restorable.
bool applyStreamService(const std::string &server, const std::string &key, std::string &error);
bool restoreStreamService(const ServiceBackup &backup, std::string &error);

bool streamingActive();
// True when OBS's current stream service points at a Bilibili ingest server.
bool streamTargetIsBilibili();
void startStreaming();
void stopStreaming();

enum class OutputEvent { Stopped, Dropped, Reconnecting, Reconnected };

// Watches the streaming output's stop/reconnect signals. `callback` runs on an
// OBS thread; marshal to the UI thread before touching anything.
// Call attach() when streaming is starting; detach() on unload.
class OutputWatcher {
public:
	explicit OutputWatcher(std::function<void(OutputEvent)> callback) : m_callback(std::move(callback)) {}
	~OutputWatcher() { detach(); }
	OutputWatcher(const OutputWatcher &) = delete;
	OutputWatcher &operator=(const OutputWatcher &) = delete;

	void attach();
	void detach();

private:
	static void onStop(void *data, ::calldata *cd);
	static void onReconnect(void *data, ::calldata *cd);
	static void onReconnectSuccess(void *data, ::calldata *cd);

	std::function<void(OutputEvent)> m_callback;
	::obs_output *m_output = nullptr;
};

} // namespace kaibo::obs
