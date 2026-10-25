// SPDX-License-Identifier: GPL-2.0-only
#pragma once

namespace kaibo {

// Bilibili can answer a start-live request with 60024 / 60043: the broadcast
// needs a face verification first. The verification happens on the phone and
// the live API only sees it a moment later, so retrying once, immediately, just
// asks for a second scan. These numbers space the automatic retries out.
constexpr int kFaceAuthMaxRetries = 5;

// Delay before retry `attempt` (0-based): 1s, 2s, 4s, then 8s from there on.
constexpr int faceAuthBackoffMs(int attempt)
{
	return attempt <= 0 ? 1000 : attempt == 1 ? 2000 : attempt == 2 ? 4000 : 8000;
}

} // namespace kaibo
