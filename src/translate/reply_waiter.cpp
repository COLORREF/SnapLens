#include "reply_waiter.h"

#include <QEventLoop>
#include <QNetworkReply>
#include <QTimer>

ReplyWaitResult waitForReply(QNetworkReply* reply, const ReplyTimeouts& timeouts,
                             std::atomic<int>* cancelFlag)
{
    QEventLoop loop;
    QTimer phaseTimer, totalTimer, cancelTimer;
    phaseTimer.setSingleShot(true);
    totalTimer.setSingleShot(true);
    ReplyWaitResult result;
    bool sent = false;
    auto arm = [](QTimer& timer, int seconds) {
        timer.stop();
        if (seconds > 0) timer.start(qMin(seconds, 86400) * 1000);
    };
    auto abort = [&](int code, const QString& message) {
        result = {code, message};
        if (!reply->isFinished()) reply->abort();
        loop.quit();
    };
    auto responseActivity = [&] {
        sent = true;
        arm(phaseTimer, timeouts.idleSeconds);
    };
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    // requestSent also fires for reused connections, unlike encrypted/socket signals.
    QObject::connect(reply, &QNetworkReply::requestSent, &loop, responseActivity);
    QObject::connect(reply, &QNetworkReply::metaDataChanged, &loop, responseActivity);
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, responseActivity);
    QObject::connect(reply, &QNetworkReply::redirected, &loop, [&] {
        sent = false;
        arm(phaseTimer, timeouts.sendSeconds);
    });
    QObject::connect(&phaseTimer, &QTimer::timeout, &loop, [&] {
        abort(-2, sent ? QStringLiteral("响应空闲超时：长时间未收到数据，可在设置中增加等待时间或设为 0")
                       : QStringLiteral("连接或发送请求超时，请检查网络和接口地址"));
    });
    QObject::connect(&totalTimer, &QTimer::timeout, &loop, [&] {
        abort(-2, QStringLiteral("获取模型资料超时"));
    });
    if (cancelFlag) {
        if (cancelFlag->load(std::memory_order_relaxed)) {
            abort(-3, QStringLiteral("用户取消"));
            return result;
        }
        cancelTimer.setInterval(100);
        QObject::connect(&cancelTimer, &QTimer::timeout, &loop, [&] {
            if (cancelFlag->load(std::memory_order_relaxed)) abort(-3, QStringLiteral("用户取消"));
        });
        cancelTimer.start();
    }
    arm(phaseTimer, timeouts.sendSeconds);
    arm(totalTimer, timeouts.totalSeconds);
    if (!reply->isFinished()) loop.exec();
    return result;
}
