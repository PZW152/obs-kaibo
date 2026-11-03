// SPDX-License-Identifier: GPL-2.0-only
#include "app/worker.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QCoreApplication>
#include <QEvent>
#include <QObject>
#include <QPointer>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>

namespace kaibo::worker {
namespace {

std::mutex g_mutex;
std::condition_variable g_cv;
std::deque<std::function<void()>> g_queue;
bool g_active = false; // a job is running right now
std::thread g_thread;
std::atomic<bool> g_stopping{false};
std::atomic<bool> g_abort{false};

// QMetaObject::invokeMethod(context, functor) needs a Qt symbol the Qt builds in
// obs-deps do not export, so completed jobs travel as posted events instead.
// Posted events are also delivered inside nested event loops (QDialog::exec()).
class JobEvent : public QEvent {
public:
	JobEvent(QPointer<QObject> context, std::function<void()> fn)
		: QEvent(type()),
		  m_context(std::move(context)),
		  m_fn(std::move(fn))
	{
	}
	static QEvent::Type type()
	{
		static const auto t = static_cast<QEvent::Type>(QEvent::registerEventType());
		return t;
	}
	void run() const
	{
		if (m_context && m_fn)
			m_fn();
	}

private:
	QPointer<QObject> m_context;
	std::function<void()> m_fn;
};

class Receiver : public QObject {
protected:
	bool event(QEvent *e) override
	{
		if (e->type() == JobEvent::type()) {
			try {
				static_cast<JobEvent *>(e)->run();
			} catch (const std::exception &error) {
				obs_log(LOG_ERROR, "UI continuation threw: %s", error.what());
			} catch (...) {
				obs_log(LOG_ERROR, "UI continuation threw an unknown exception");
			}
			return true;
		}
		return QObject::event(e);
	}
};

// Intentionally leaked: it lives on the UI thread and OBS destroys the application
// object after the module is unloaded; deleting it there would be worse than leaking
// one QObject for the rest of the process.
QObject *receiver()
{
	static QObject *r = new Receiver();
	return r;
}

bool idleLocked()
{
	return g_queue.empty() && !g_active;
}

void loop()
{
	for (;;) {
		std::function<void()> job;
		{
			std::unique_lock<std::mutex> lock(g_mutex);
			g_cv.wait(lock, [] { return g_stopping.load() || !g_queue.empty(); });
			if (g_stopping.load()) {
				g_cv.notify_all(); // waitIdle() observes the empty queue
				return;
			}
			job = std::move(g_queue.front());
			g_queue.pop_front();
			g_active = true;
		}
		try {
			job();
		} catch (const std::exception &error) {
			obs_log(LOG_ERROR, "background job threw: %s", error.what());
		} catch (...) {
			obs_log(LOG_ERROR, "background job threw an unknown exception");
		}
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_active = false;
		}
		g_cv.notify_all();
	}
}

} // namespace

void start()
{
	(void)receiver(); // created on the UI thread, so it lives there
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_thread.joinable())
		return;
	g_stopping = false;
	g_abort = false;
	g_active = false;
	g_thread = std::thread(loop);
}

void shutdown()
{
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_stopping = true;
		g_abort = true; // make an in-flight transfer abort instead of timing out
		g_queue.clear();
	}
	g_cv.notify_all();
	// Join rather than detach: once this returns no background job can still run plugin
	// code, log, or post a Qt event while OBS tears the module down. The transport
	// watches shuttingDown() through libcurl's progress callback, so a request in
	// flight gives up quickly instead of holding the exit for its full timeout.
	if (g_thread.joinable())
		g_thread.join();
}

bool shuttingDown()
{
	return g_stopping.load();
}

void abortInFlight()
{
	g_abort = true;
}

bool cancelled()
{
	return g_stopping.load() || g_abort.load();
}

bool waitIdle(int timeoutMs)
{
	std::unique_lock<std::mutex> lock(g_mutex);
	return g_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [] { return idleLocked(); });
}

void postToUi(QObject *context, std::function<void()> fn)
{
	if (!fn || shuttingDown())
		return;
	QCoreApplication::postEvent(receiver(), new JobEvent(QPointer<QObject>(context), std::move(fn)));
}

void run(QObject *context, std::function<void()> work, std::function<void()> done)
{
	QPointer<QObject> guard(context);
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_stopping.load())
		return;
	g_queue.push_back([work = std::move(work), done = std::move(done), guard]() mutable {
		try {
			work();
		} catch (const std::exception &error) {
			// `done` still runs: callers rely on it to clear their busy state.
			obs_log(LOG_ERROR, "background job threw: %s", error.what());
		} catch (...) {
			obs_log(LOG_ERROR, "background job threw an unknown exception");
		}
		if (done && !shuttingDown())
			QCoreApplication::postEvent(receiver(), new JobEvent(guard, std::move(done)));
	});
	g_cv.notify_one();
}

} // namespace kaibo::worker
