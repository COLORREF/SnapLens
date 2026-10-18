#pragma once

#include <QString>
#include <atomic>

class QNetworkReply;

/** Timeout phases are independent. Zero disables a phase deadline.
 * Chat uses sendSeconds and optional idleSeconds, with no total deadline.
 * Metadata discovery uses totalSeconds because it has no inference phase.
 */
struct ReplyTimeouts {
    int sendSeconds = 30;
    int idleSeconds = 0;
    int totalSeconds = 0;
};

struct ReplyWaitResult {
    int errorCode = 0; // 0: reply finished; -2: deadline; -3: caller cancelled.
    QString error;
};

/// Consumes events in the reply's thread; does not read bytes or own the reply.
/// Receiving any bytes (including SSE comments/reasoning) resets the idle timer.
ReplyWaitResult waitForReply(QNetworkReply* reply, const ReplyTimeouts& timeouts,
                             std::atomic<int>* cancelFlag = nullptr);
