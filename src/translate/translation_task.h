#pragma once
#include <QObject>
#include <QPointer>

/**
 * @brief 取消并释放界面对翻译任务的引用，不阻塞 GUI 线程。
 * @details 调用方须在启动前连接 finished → deleteLater；任务持有自己的配置快照。
 * 此函数在 GUI 线程调用，结果接收者销毁和重复取消均可安全处理。
 */
template<class Service>
void releaseTranslationTask(QPointer<Service>& service, QObject* receiver)
{
    auto* active = service.data();
    service.clear();
    if (!active) return;
    active->requestCancel();
    QObject::disconnect(active, nullptr, receiver, nullptr);
    if (!active->isRunning()) active->deleteLater();
}
