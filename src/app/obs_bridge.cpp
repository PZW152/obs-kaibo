// SPDX-License-Identifier: GPL-2.0-only
#include "app/obs_bridge.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

namespace kaibo::obs {
namespace {

bool isBilibiliServer(const char *server)
{
	if (!server)
		return false;
	const std::string s = server;
	return s.find("bilivideo.com") != std::string::npos || s.find("acg.tv") != std::string::npos;
}

} // namespace

// Captures the service Kaibo is about to replace. The backup can be persisted across
// sessions (crash while live), so it must be refreshed whenever the current service is a
// real user service again — "a backup exists" does not mean "it is still the right one".
static void captureService(ServiceBackup &backup, obs_service_t *current)
{
	obs_data_t *settings = obs_service_get_settings(current);
	const char *type = obs_service_get_type(current);
	const char *currentServer = obs_data_get_string(settings, "server");
	if (type && !isBilibiliServer(currentServer)) {
		backup.type = type;
		backup.settingsJson = obs_data_get_json(settings);
	}
	obs_data_release(settings);
}

bool applyStreamService(const std::string &server, const std::string &key, ServiceBackup *backup, std::string &error)
{
	if (obs_frontend_streaming_active()) {
		error = "streaming is active";
		return false;
	}

	if (backup) {
		if (obs_service_t *current = obs_frontend_get_streaming_service())
			captureService(*backup, current);
	}

	obs_data_t *settings = obs_data_create();
	obs_data_set_string(settings, "server", server.c_str());
	obs_data_set_string(settings, "key", key.c_str());
	obs_data_set_bool(settings, "use_auth", false);
	obs_data_set_bool(settings, "bwtest", false);
	obs_service_t *service = obs_service_create("rtmp_custom", "default_service", settings, nullptr);
	obs_data_release(settings);
	if (!service) {
		error = "could not create rtmp_custom service";
		return false;
	}
	obs_frontend_set_streaming_service(service);
	obs_frontend_save_streaming_service();
	obs_service_release(service);
	return true;
}

bool restoreStreamService(const ServiceBackup &backup, std::string &error)
{
	if (!backup.valid()) {
		error = "no backup";
		return false;
	}
	if (obs_frontend_streaming_active()) {
		error = "streaming is active";
		return false;
	}
	obs_data_t *settings = obs_data_create_from_json(backup.settingsJson.c_str());
	if (!settings)
		settings = obs_data_create();
	obs_service_t *service = obs_service_create(backup.type.c_str(), "default_service", settings, nullptr);
	obs_data_release(settings);
	if (!service) {
		error = "could not recreate service " + backup.type;
		return false;
	}
	obs_frontend_set_streaming_service(service);
	obs_frontend_save_streaming_service();
	obs_service_release(service);
	return true;
}

bool streamTargetIsBilibili()
{
	obs_service_t *service = obs_frontend_get_streaming_service();
	if (!service)
		return false;
	obs_data_t *settings = obs_service_get_settings(service);
	const bool result = isBilibiliServer(obs_data_get_string(settings, "server"));
	obs_data_release(settings);
	return result;
}

bool streamingActive()
{
	return obs_frontend_streaming_active();
}

void startStreaming()
{
	if (!obs_frontend_streaming_active())
		obs_frontend_streaming_start();
}

void stopStreaming()
{
	if (obs_frontend_streaming_active())
		obs_frontend_streaming_stop();
}

void OutputWatcher::attach()
{
	obs_output_t *output = obs_frontend_get_streaming_output();
	if (!output)
		return;
	if (output == m_output) {
		obs_output_release(output); // already watching this one
		return;
	}
	detach();
	m_output = output; // keep the reference until detach()
	signal_handler_t *handler = obs_output_get_signal_handler(output);
	signal_handler_connect(handler, "stop", &OutputWatcher::onStop, this);
	signal_handler_connect(handler, "reconnect", &OutputWatcher::onReconnect, this);
	signal_handler_connect(handler, "reconnect_success", &OutputWatcher::onReconnectSuccess, this);
}

void OutputWatcher::detach()
{
	if (!m_output)
		return;
	signal_handler_t *handler = obs_output_get_signal_handler(m_output);
	signal_handler_disconnect(handler, "stop", &OutputWatcher::onStop, this);
	signal_handler_disconnect(handler, "reconnect", &OutputWatcher::onReconnect, this);
	signal_handler_disconnect(handler, "reconnect_success", &OutputWatcher::onReconnectSuccess, this);
	obs_output_release(m_output);
	m_output = nullptr;
}

void OutputWatcher::onStop(void *data, calldata_t *cd)
{
	auto *self = static_cast<OutputWatcher *>(data);
	const long long code = calldata_int(cd, "code");
	// OBS_OUTPUT_SUCCESS means a deliberate stop; anything else is a failure/disconnect.
	self->m_callback(code == OBS_OUTPUT_SUCCESS ? OutputEvent::Stopped : OutputEvent::Dropped);
}

void OutputWatcher::onReconnect(void *data, calldata_t *)
{
	static_cast<OutputWatcher *>(data)->m_callback(OutputEvent::Reconnecting);
}

void OutputWatcher::onReconnectSuccess(void *data, calldata_t *)
{
	static_cast<OutputWatcher *>(data)->m_callback(OutputEvent::Reconnected);
}

} // namespace kaibo::obs
