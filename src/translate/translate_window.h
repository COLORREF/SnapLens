#ifndef SNAPLENS_TRANSLATE_TRANSLATE_WINDOW_H
#define SNAPLENS_TRANSLATE_TRANSLATE_WINDOW_H

#include "ElaWidgetTools.hpp"

#include <QPixmap>
#include <QString>
#include <QPointer>

class QComboBox;
class QPushButton;
class QLabel;
class QPlainTextEdit;
class QSplitter;
class QStackedWidget;
class Settings;
class NotifyManager;
class TranslateService;
class ZoomableView;

/** @brief 截图翻译窗口；关闭或销毁时取消所属任务，任务自行完成收尾。 */
class TranslateWindow : public ElaWindow
{
    Q_OBJECT

public:
    explicit TranslateWindow(const QPixmap& pixmap, Settings* settings,
                             bool autoTranslate = true,
                             NotifyManager* notifyManager = nullptr,
                             QWidget* parent = nullptr);
    ~TranslateWindow() override;

    // 是否有翻译任务正在运行（供设置保存时"下次生效"提示检测）
    bool isBusy() const;

signals:
    void closed();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onTranslated(const QString& text);
    void onOcrText(const QString& text);
    void onThinking(const QString& text);
    void onError(const QString& msg);
    void startTranslation();
    void copyTranslated();
    void copyOcrText();

private:
    void buildUi();
    void launchService();

    QPixmap         m_pixmap;
    Settings*       m_settings;
    bool            m_autoTranslate;
    NotifyManager*  m_notifyManager;

    QComboBox*      m_langCombo = nullptr;
    QPushButton*    m_translateBtn = nullptr;
    QPushButton*    m_copyTransBtn = nullptr;
    QPushButton*    m_copyOcrBtn = nullptr;
    QLabel*         m_statusLabel = nullptr;
    ZoomableView*   m_imageView = nullptr;
    ElaPivot*       m_pivot = nullptr;
    QStackedWidget* m_stack = nullptr;
    QPlainTextEdit* m_transEdit = nullptr;
    QPlainTextEdit* m_ocrEdit = nullptr;
    QPlainTextEdit* m_thinkEdit = nullptr;
    ElaProgressRing* m_progressRing = nullptr;

    // 线程对象通过 finished → deleteLater 自动删除，QPointer 确保删除后
    // 成员自动置空，杜绝 closeEvent/startTranslation 访问悬空指针（use-after-free）。
    QPointer<TranslateService> m_service;
    bool m_translating = false;
    QString m_ocrText;
};

#endif
