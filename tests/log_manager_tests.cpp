#include "log/log_manager.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QLockFile>
#include <QProcess>
#include <cstdio>
#include <thread>
#include <vector>

namespace {
int failures = 0;
int restoredMessages = 0;
void check(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
QByteArray read(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
void previousHandler(QtMsgType, const QMessageLogContext&, const QString&)
{
    ++restoredMessages;
}
}

/** @brief 验证对业务可见的日志契约，不依赖用户配置或用户日志目录。 */
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    // 子进程验证真实的 stderr 管道，文件输出成功时也必须产生控制台日志。
    if (app.arguments().contains("--console-fixture")) {
        LogOptions fixture;
        fixture.filePath = app.arguments().last();
        fixture.debugOutput = false;
        LogSession session(fixture);
        SNAP_LOG_INFO(QStringLiteral("console-and-file 中文"));
        return 0;
    }
    QTemporaryDir temp;
    check(temp.isValid(), "temporary directory");
    QProcess console;
    const auto consoleFile = temp.filePath("console.log");
    console.start(app.applicationFilePath(), {"--console-fixture", consoleFile});
    check(console.waitForFinished(10000), "console fixture completed");
    check(console.exitStatus() == QProcess::NormalExit && console.exitCode() == 0,
          "console fixture exit status");
    const auto marker = QStringLiteral("console-and-file 中文").toUtf8();
    check(console.readAllStandardError().contains(marker), "stderr missing with healthy file sink");
    check(read(consoleFile).contains(marker), "file missing with console sink enabled");
    auto& log = LogManager::instance();
    const auto original = qInstallMessageHandler(previousHandler);
    LogOptions options;
    options.filePath = temp.filePath(QStringLiteral("中文目录/日志.log"));
    options.debugOutput = false;
    options.consoleOutput = false;
    check(log.init(options), "open Unicode file path");
    log.applySettings(true, false, true, true);
    int evaluations = 0;
    SNAP_LOG_INFO(QString::number(++evaluations));
    check(evaluations == 0, "disabled messages must not be evaluated");
    SNAP_LOG_DEBUG(QStringLiteral("中文消息 {literal braces}"));
    qInfo("filtered Qt message");
    qWarning("bridged Qt warning");
    log.flush();
    auto bytes = read(options.filePath);
    check(bytes.contains(QStringLiteral("中文消息 {literal braces}").toUtf8()), "UTF-8 and literal braces");
    check(bytes.contains("log_manager_tests.cpp:"), "call-site source location");
    check(bytes.contains("bridged Qt warning"), "Qt bridge");
    check(!bytes.contains("filtered Qt message"), "independent Qt severity filtering");
    log.applySettings(false, false, false, false);
    log.write(LogLevel::Fatal, __FILE__, __LINE__, __FUNCTION__, "fatal survives filters");
    check(read(options.filePath).contains("fatal survives filters"), "fatal must flush synchronously");

    log.applySettings(true, true, true, true);
    std::vector<std::thread> writers;
    for (int thread = 0; thread < 4; ++thread) {
        writers.emplace_back([thread] {
            for (int i = 0; i < 100; ++i)
                SNAP_LOG_INFO(QString("concurrent-%1-%2-END").arg(thread).arg(i));
        });
    }
    for (auto& writer : writers) writer.join();
    log.shutdown();
    log.shutdown();
    bytes = read(options.filePath);
    check(bytes.count("concurrent-") == 400, "concurrent records lost or duplicated");
    for (int thread = 0; thread < 4; ++thread)
        for (int i = 0; i < 100; ++i)
            check(bytes.contains(QString("concurrent-%1-%2-END").arg(thread).arg(i).toUtf8()),
                  "concurrent record corrupted");
    qInfo("restored handler");
    check(restoredMessages == 1, "previous Qt handler restored");
    check(!QFile::exists(options.filePath + ".lock"), "file lock released on shutdown");

    options.filePath = temp.filePath("rotation.log");
    options.maxFileBytes = 512;
    options.maxFiles = 2;
    check(log.init(options), "reinitialize for rotation");
    for (int i = 0; i < 100; ++i) SNAP_LOG_INFO(QString("rotation-%1").arg(i));
    log.shutdown();
    check(QDir(temp.path()).entryList({"rotation*.log"}, QDir::Files).size() == 3,
          "rotation must retain current file and two archives");
    check(read(options.filePath).contains("rotation-99"), "latest record retained");
    options.truncate = true;
    check(log.init(options), "regression file initialization");
    SNAP_LOG_INFO("fresh regression record");
    log.shutdown();
    check(!read(options.filePath).contains("rotation-99"), "regression file truncation");

    QLockFile otherProcess(options.filePath + ".lock");
    check(otherProcess.tryLock(), "fixture log lock");
    check(!log.init(options), "must not overwrite another process's log");
    SNAP_LOG_WARNING("locked-file fallback");
    log.shutdown();
    otherProcess.unlock();
    options.filePath = temp.path(); // A directory cannot be opened as a log file.
    check(!log.init(options), "invalid output path must report failure");
    SNAP_LOG_ERROR("unwritable-file fallback");
    log.shutdown();
    qInstallMessageHandler(original);
    std::printf("Logging checks: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
