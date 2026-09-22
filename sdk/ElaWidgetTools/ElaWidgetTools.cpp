#include "ElaWidgetTools.hpp"

ElaEventBusType::EventBusReturnType ElaEvent::elaDeferred_registerAndInit_0()
{
    return ElaEventBus::getInstance()->registerEvent(this);
}

void ElaEvent::elaDeferredDestroyElaEvent() { ElaEventBus::getInstance()->unRegisterEvent(this); }

void ElaListViewStyle::elaDeferred_drawControl_0(ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    switch (element)
    {
    case QStyle::CE_ShapedFrame:
    {
        // viewport视口外的其他区域背景
        if (!_pIsTransparent)
        {
            QRect frameRect = option->rect;
            frameRect.adjust(1, 1, -1, -1);
            painter->save();
            painter->setRenderHints(QPainter::Antialiasing);
            painter->setPen(ElaThemeColor(_themeMode, PopupBorder));
            painter->setBrush(ElaThemeColor(_themeMode, BasicBaseAlpha));
            painter->drawRoundedRect(frameRect, 3, 3);
            painter->restore();
        }
        return;
    }
    case QStyle::CE_ItemViewItem:
    {
        if (const auto* vopt = qstyleoption_cast<const QStyleOptionViewItem*>(option))
        {
            // 背景绘制
            this->drawPrimitive(QStyle::PE_PanelItemViewItem, option, painter, widget);

            // 内容绘制
            QRect itemRect = option->rect;
            painter->save();
            painter->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
            const auto* listView = dynamic_cast<const ElaListView*>(widget);
            QListView::ViewMode viewMode = listView->viewMode();
            // QRect checkRect = proxy()->subElementRect(SE_ItemViewItemCheckIndicator, vopt, widget);
            QRect iconRect = proxy()->subElementRect(SE_ItemViewItemDecoration, vopt, widget);
            QRect textRect = proxy()->subElementRect(SE_ItemViewItemText, vopt, widget);
            iconRect.adjust(_leftPadding, 0, 0, 0);
            textRect.adjust(_leftPadding, 0, 0, 0);
            // 图标绘制
            if (!vopt->icon.isNull())
            {
                QIcon::Mode mode = QIcon::Normal;
                if (!(vopt->state.testFlag(QStyle::State_Enabled)))
                {
                    mode = QIcon::Disabled;
                }
                else if (vopt->state.testFlag(QStyle::State_Selected))
                {
                    mode = QIcon::Selected;
                }
                QIcon::State state = vopt->state & QStyle::State_Open ? QIcon::On : QIcon::Off;
                vopt->icon.paint(painter, iconRect, vopt->decorationAlignment, mode, state);
            }
            // 文字绘制
            if (!vopt->text.isEmpty())
            {
                painter->setPen(ElaThemeColor(_themeMode, BasicText));
                painter->drawText(textRect, vopt->displayAlignment, vopt->text);
            }
            // 选中特效
            if (vopt->state.testFlag(QStyle::State_Selected) && viewMode == QListView::ListMode)
            {
                int heightOffset = itemRect.height() / 4;
                painter->setPen(Qt::NoPen);
                painter->setBrush(ElaThemeColor(_themeMode, PrimaryNormal));
                painter->drawRoundedRect(QRectF(itemRect.x() + 3, itemRect.y() + heightOffset, 3, itemRect.height() - 2 * heightOffset), 3, 3);
            }
            painter->restore();
        }

        return;
    }
    default:
    {
        break;
    }
    }
    QProxyStyle::drawControl(element, option, painter, widget);
}

QSize ElaListViewStyle::elaDeferred_sizeFromContents_1(ContentsType type, const QStyleOption* option, const QSize& size, const QWidget* widget) const
{
    switch (type)
    {
    case QStyle::CT_ItemViewItem:
    {
        QSize itemSize = QProxyStyle::sizeFromContents(type, option, size, widget);
        const auto* listView = dynamic_cast<const ElaListView*>(widget);
        QListView::ViewMode viewMode = listView->viewMode();
        if (viewMode == QListView::ListMode)
        {
            itemSize.setWidth(itemSize.width() + _leftPadding);
        }
        itemSize.setHeight(_pItemHeight);
        return itemSize;
    }
    default:
    {
        break;
    }
    }
    return QProxyStyle::sizeFromContents(type, option, size, widget);
}

void ElaNavigationStyle::elaDeferred_ElaNavigationStyle_0(QStyle* style)
{
    _pOpacity = 1;
    _pItemHeight = 38;
    _pLastSelectMarkTop = 10.5;
    _pLastSelectMarkBottom = 10.5;
    _pSelectMarkTop = 10.5;
    _pSelectMarkBottom = 10.5;

    // Mark向上
    _lastSelectMarkTopAnimation = new QPropertyAnimation(this, "pLastSelectMarkTop");
    connect(
        _lastSelectMarkTopAnimation,
        &QPropertyAnimation::valueChanged,
        this,
        [this](const QVariant&) {
            if (auto* view = static_cast<ElaNavigationView*>(_pNavigationView))
                view->viewport()->update();
        }
    );
    _lastSelectMarkTopAnimation->setDuration(300);
    _lastSelectMarkTopAnimation->setEasingCurve(QEasingCurve::InOutSine);

    _selectMarkBottomAnimation = new QPropertyAnimation(this, "pSelectMarkBottom");
    connect(
        _selectMarkBottomAnimation,
        &QPropertyAnimation::valueChanged,
        this,
        [this](const QVariant&) {
            if (auto* view = static_cast<ElaNavigationView*>(_pNavigationView))
                view->viewport()->update();
        }
    );
    _selectMarkBottomAnimation->setDuration(300);
    _selectMarkBottomAnimation->setEasingCurve(QEasingCurve::InOutSine);
    connect(
        _lastSelectMarkTopAnimation,
        &QPropertyAnimation::finished,
        this,
        [=]() {
            _isSelectMarkDisplay = true;
            _lastSelectedNode = nullptr;
            _selectMarkBottomAnimation->setStartValue(0);
            _selectMarkBottomAnimation->setEndValue(10.5);
            _selectMarkBottomAnimation->start();
        }
    );

    // Mark向下
    _lastSelectMarkBottomAnimation = new QPropertyAnimation(this, "pLastSelectMarkBottom");
    connect(
        _lastSelectMarkBottomAnimation,
        &QPropertyAnimation::valueChanged,
        this,
        [this](const QVariant&) {
            if (auto* view = static_cast<ElaNavigationView*>(_pNavigationView))
                view->viewport()->update();
        }
    );
    _lastSelectMarkBottomAnimation->setDuration(300);
    _lastSelectMarkBottomAnimation->setEasingCurve(QEasingCurve::InOutSine);

    _selectMarkTopAnimation = new QPropertyAnimation(this, "pSelectMarkTop");
    connect(
        _selectMarkTopAnimation,
        &QPropertyAnimation::valueChanged,
        this,
        [this](const QVariant&) {
            if (auto* view = static_cast<ElaNavigationView*>(_pNavigationView))
                view->viewport()->update();
        }
    );
    _selectMarkTopAnimation->setDuration(300);
    _selectMarkTopAnimation->setEasingCurve(QEasingCurve::InOutSine);
    connect(
        _lastSelectMarkBottomAnimation,
        &QPropertyAnimation::finished,
        this,
        [=]() {
            _isSelectMarkDisplay = true;
            _lastSelectedNode = nullptr;
            _selectMarkTopAnimation->setStartValue(0.0);
            _selectMarkTopAnimation->setEndValue(10.5);
            _selectMarkTopAnimation->start();
        }
    );
    _themeMode = eTheme->getThemeMode();
    connect(eTheme, &ElaTheme::themeModeChanged, this, [=](ElaThemeType::ThemeMode themeMode) { _themeMode = themeMode; });
}

void ElaNavigationStyle::elaDeferred_drawControl_1(ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    int fontPixelSize = eApp->getFontPixelSize();
    switch (element)
    {
    case QStyle::CE_ShapedFrame:
    {
        // viewport视口外的其他区域背景
        return;
    }
    case QStyle::CE_ItemViewItem:
    {
        if (const auto* vopt = qstyleoption_cast<const QStyleOptionViewItem*>(option))
        {
            // 背景绘制
            this->drawPrimitive(QStyle::PE_PanelItemViewItem, option, painter, widget);

            // 内容绘制
            QRect itemRect = option->rect;
            painter->save();
            painter->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
            auto* node = static_cast<ElaNavigationNode*>(vopt->index.internalPointer());
            auto* model = dynamic_cast<ElaNavigationModel*>(const_cast<QAbstractItemModel*>(vopt->index.model()));

            if (this->_opacityAnimationTargetNode && node->getParentNode() == this->_opacityAnimationTargetNode)
            {
                painter->setOpacity(_pOpacity);
            }

            // 选中特效
            if (_isSelectMarkDisplay && (node == model->getSelectedNode() || node == model->getSelectedExpandedNode()))
            {
                painter->setPen(Qt::NoPen);
                painter->setBrush(ElaThemeColor(_themeMode, PrimaryNormal));
                painter->drawRoundedRect(
                    QRectF(itemRect.x() + 3, itemRect.y() + _pSelectMarkTop, 3, itemRect.height() - _pSelectMarkTop - _pSelectMarkBottom),
                    1.5,
                    1.5
                );
            }
            if (node == _lastSelectedNode)
            {
                painter->setPen(Qt::NoPen);
                painter->setBrush(ElaThemeColor(_themeMode, PrimaryNormal));
                painter->drawRoundedRect(
                    QRectF(itemRect.x() + 3, itemRect.y() + _pLastSelectMarkTop, 3, itemRect.height() - _pLastSelectMarkTop - _pLastSelectMarkBottom),
                    1.5,
                    1.5
                );
            }

            // 图标绘制
            painter->setPen(vopt->index == _pPressIndex ? ElaThemeColor(_themeMode, BasicTextPress) : ElaThemeColor(_themeMode, BasicText));
            if (node->getAwesome() != ElaIconType::None)
            {
                painter->save();
                QFont iconFont;
            iconFont.setFamily(QStringLiteral("ElaAwesome"));
                iconFont.setPixelSize(fontPixelSize + 4);
                painter->setFont(iconFont);
                painter->drawText(
                    QRect(itemRect.x(), itemRect.y(), _iconAreaWidth, itemRect.height()),
                    Qt::AlignCenter,
                    ElaIconChar(node->getAwesome())
                );
                painter->restore();
            }

            int viewWidth = widget->width();
            // 文字绘制
            if (node->getIsCategoryNode())
            {
                QFont categoryFont = painter->font();
                categoryFont.setBold(true);
                painter->setFont(categoryFont);
                painter->setPen(ElaThemeColor(_themeMode, BasicTextCategory));
            }
            else
            {
                painter->setPen((vopt->index == _pPressIndex) ? ElaThemeColor(_themeMode, BasicTextPress) : ElaThemeColor(_themeMode, BasicText));
            }
            QRect textRect;
            if (node->getAwesome() != ElaIconType::None)
            {
                textRect = QRect(
                    itemRect.x() + _iconAreaWidth,
                    itemRect.y(),
                    itemRect.width() - _textRightSpacing - _indicatorIconAreaWidth - _iconAreaWidth,
                    itemRect.height()
                );
            }
            else
            {
                textRect = QRect(
                    itemRect.x() + _leftPadding,
                    itemRect.y(),
                    itemRect.width() - _textRightSpacing - _indicatorIconAreaWidth - _leftPadding,
                    itemRect.height()
                );
            }
            QString text = painter->fontMetrics().elidedText(node->getNodeTitle(), Qt::ElideRight, textRect.width());
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);

            if (viewWidth > _pNavigationView->getNavigationBarPrivate()->_pNavigationBarWidth - _indicatorIconAreaWidth)
            {
                // 展开图标 KeyPoints
                if (node->getIsExpanderNode())
                {
                    if (node->getIsHasChild())
                    {
                        QRectF expandIconRect(itemRect.right() - _indicatorIconAreaWidth, itemRect.y(), 17, itemRect.height());
                        painter->save();
                        QFont iconFont;
            iconFont.setFamily(QStringLiteral("ElaAwesome"));
                        iconFont.setPixelSize(fontPixelSize + 4);
                        painter->setFont(iconFont);
                        painter->translate(
                            expandIconRect.x() + (qreal)expandIconRect.width() / 2,
                            expandIconRect.y() + (qreal)expandIconRect.height() / 2
                        );
                        if (node == _expandAnimationTargetNode)
                        {
                            painter->rotate(_pRotate);
                        }
                        else
                        {
                            if (node->getIsExpanded())
                            {
                                // 展开
                                painter->rotate(-180);
                            }
                            else
                            {
                                // 未展开
                                painter->rotate(0);
                            }
                        }
                        painter->translate(
                            -expandIconRect.x() - (qreal)expandIconRect.width() / 2 + 1,
                            -expandIconRect.y() - (qreal)expandIconRect.height() / 2
                        );
                        painter->drawText(expandIconRect, Qt::AlignVCenter, ElaIconChar(ElaIconType::AngleDown));
                        painter->restore();
                    }
                    if (node->getIsChildHasKeyPoints())
                    {
                        painter->save();
                        painter->setPen(Qt::NoPen);
                        painter->setBrush(ElaThemeColor(_themeMode, PrimaryNormal));
                        painter->drawEllipse(QPoint(itemRect.right() - 17, itemRect.y() + 12), 3, 3);
                        painter->restore();
                    }
                }
                else
                {
                    if (int keyPoints = node->getKeyPoints())
                    {
                        // KeyPoints
                        painter->save();
                        painter->setPen(Qt::NoPen);
                        painter->setBrush(ElaThemeColor(_themeMode, PrimaryNormal));
                        int keyPointRadius = 8;
                        painter->drawEllipse(QPoint(itemRect.right() - 26, itemRect.y() + itemRect.height() / 2), keyPointRadius, keyPointRadius);
                        painter->setPen(QPen(ElaThemeColor(_themeMode, BasicTextInvert), 2));
                        QFont font = painter->font();
                        font.setBold(true);
                        if (keyPoints > 99)
                        {
                            keyPoints = 99;
                        }
                        if (keyPoints > 9)
                        {
                            font.setPixelSize(fontPixelSize - 3);
                        }
                        else
                        {
                            font.setPixelSize(fontPixelSize - 2);
                        }
                        painter->setFont(font);
                        painter->drawText(
                            QRect(
                                QPoint(itemRect.right() - 26 - keyPointRadius, itemRect.y() + itemRect.height() / 2 - keyPointRadius),
                                QSize(2 * keyPointRadius, 2 * keyPointRadius)
                            ),
                            Qt::AlignCenter,
                            QString::number(keyPoints)
                        );
                        painter->restore();
                    }
                }
            }
            painter->restore();
        }

        return;
    }
    default:
    {
        break;
    }
    }
    QProxyStyle::drawControl(element, option, painter, widget);
}

void ElaNavigationStyle::elaDeferred_navigationNodeStateChange_2(const QVariantMap& data)
{
    if (data.contains("Expand"))
    {
        ElaNavigationNode* lastExpandNode = _expandAnimationTargetNode;
        _opacityAnimationTargetNode = data.value("Expand").value<ElaNavigationNode*>();
        _expandAnimationTargetNode = _opacityAnimationTargetNode;
        auto* nodeOpacityAnimation = new QPropertyAnimation(this, "pOpacity");
        connect(nodeOpacityAnimation, &QPropertyAnimation::finished, this, [=]() { _opacityAnimationTargetNode = nullptr; });
        connect(
            nodeOpacityAnimation,
            &QPropertyAnimation::valueChanged,
            this,
            [this](const QVariant&) {
                if (auto* view = static_cast<ElaNavigationView*>(_pNavigationView))
                    view->viewport()->update();
            }
        );
        nodeOpacityAnimation->setDuration(480);
        nodeOpacityAnimation->setEasingCurve(QEasingCurve::InOutSine);
        nodeOpacityAnimation->setKeyValueAt(0.5, 0);
        nodeOpacityAnimation->setStartValue(0);
        nodeOpacityAnimation->setEndValue(1);
        nodeOpacityAnimation->start(QAbstractAnimation::DeleteWhenStopped);

        auto* rotateAnimation = new QPropertyAnimation(this, "pRotate");
        connect(rotateAnimation, &QPropertyAnimation::finished, this, [=]() { _expandAnimationTargetNode = nullptr; });
        connect(
            rotateAnimation,
            &QPropertyAnimation::valueChanged,
            this,
            [this](const QVariant&) {
                if (auto* view = static_cast<ElaNavigationView*>(_pNavigationView))
                    view->viewport()->update();
            }
        );
        rotateAnimation->setDuration(300);
        rotateAnimation->setEasingCurve(QEasingCurve::InOutSine);
        if (lastExpandNode == _expandAnimationTargetNode)
        {
            rotateAnimation->setStartValue(_pRotate);
        }
        else
        {
            rotateAnimation->setStartValue(0);
        }
        rotateAnimation->setEndValue(-180);
        rotateAnimation->start(QAbstractAnimation::DeleteWhenStopped);
    }
    else if (data.contains("Collapse"))
    {
        ElaNavigationNode* lastExpandNode = _expandAnimationTargetNode;
        _opacityAnimationTargetNode = data.value("Collapse").value<ElaNavigationNode*>();
        _expandAnimationTargetNode = _opacityAnimationTargetNode;
        _pOpacity = 0;

        auto* rotateAnimation = new QPropertyAnimation(this, "pRotate");
        connect(
            rotateAnimation,
            &QPropertyAnimation::finished,
            this,
            [=]() {
                _pOpacity = 1;
                _expandAnimationTargetNode = nullptr;
            }
        );
        connect(
            rotateAnimation,
            &QPropertyAnimation::valueChanged,
            this,
            [this](const QVariant&) {
                if (auto* view = static_cast<ElaNavigationView*>(_pNavigationView))
                    view->viewport()->update();
            }
        );
        rotateAnimation->setDuration(300);
        rotateAnimation->setEasingCurve(QEasingCurve::InOutSine);
        if (lastExpandNode == _expandAnimationTargetNode)
        {
            rotateAnimation->setStartValue(_pRotate);
        }
        else
        {
            rotateAnimation->setStartValue(-180);
        }
        rotateAnimation->setEndValue(0);
        rotateAnimation->start(QAbstractAnimation::DeleteWhenStopped);
    }
    else if (data.contains("SelectMarkChanged"))
    {
        _lastSelectedNode = data.value("LastSelectedNode").value<ElaNavigationNode*>();
        auto* selectedNode = data.value("SelectedNode").value<ElaNavigationNode*>();
        bool direction = _compareItemY(selectedNode, _lastSelectedNode);
        _pLastSelectMarkTop = 10.5;
        _pLastSelectMarkBottom = 10.5;
        _pSelectMarkTop = 10.5;
        _pSelectMarkBottom = 10.5;
        if (direction)
        {
            _lastSelectMarkTopAnimation->setStartValue(10.5);
            _lastSelectMarkTopAnimation->setEndValue(0.0);
            _lastSelectMarkTopAnimation->start();
            _lastSelectMarkBottomAnimation->stop();
            _selectMarkTopAnimation->stop();
            _isSelectMarkDisplay = false;
        }
        else
        {
            _lastSelectMarkBottomAnimation->setStartValue(10.5);
            _lastSelectMarkBottomAnimation->setEndValue(0.0);
            _lastSelectMarkBottomAnimation->start();
            _lastSelectMarkTopAnimation->stop();
            _selectMarkBottomAnimation->stop();
            _isSelectMarkDisplay = false;
        }
    }
}

void ElaPopularCard::elaDeferred_ElaPopularCard_0(QWidget* parent)
{
    setFixedSize(320, 120);
    _pBorderRadius = 8;
    _pHoverYOffset = 0;
    _pHoverOpacity = 0;
    setObjectName("ElaPopularCard");
    setStyleSheet("#ElaPopularCard{background-color:transparent}");
    setMouseTracking(true);

    _pCardFloatArea = parentWidget();
    _floater = new ElaPopularCardFloater(this, this, _pCardFloatArea);
    _floatTimer = new QTimer(this);
    connect(_floatTimer, &QTimer::timeout, this, &ElaPopularCard::_showFloater);

    _themeMode = eTheme->getThemeMode();
    connect(eTheme, &ElaTheme::themeModeChanged, this, [=](ElaThemeType::ThemeMode themeMode) { _themeMode = themeMode; });
}

void ElaPopularCard::elaDeferredShowFloater()
{
    this->update();
    _isFloating = true;
    _pHoverYOffset = 0;
    _floatTimer->stop();
    _floater->showFloater();
}

void ElaPopularCard::elaDeferred_paintEvent_2(QPaintEvent* event)
{
    int fontPixelSize = eApp->getFontPixelSize();
    if (_isFloating)
    {
        return;
    }
    QPainter painter(this);
    painter.save();
    painter.setRenderHints(QPainter::SmoothPixmapTransform | QPainter::Antialiasing | QPainter::TextAntialiasing);
    if (underMouse())
    {
        // 阴影绘制
        painter.setOpacity(_pHoverOpacity);
        const int hoverOffset = qRound(_pHoverYOffset);
        QRect shadowRect = rect().adjusted(0, -hoverOffset, 0, -hoverOffset);
        eTheme->drawEffectShadow(&painter, shadowRect, _shadowBorderWidth, _pBorderRadius);
    }
    QRectF foregroundRect(
        _shadowBorderWidth,
        _shadowBorderWidth - _pHoverYOffset + 1,
        width() - 2 * _shadowBorderWidth,
        height() - 2 * _shadowBorderWidth
    );
    // 背景绘制
    painter.setOpacity(1);
    painter.setPen(underMouse() ? ElaThemeColor(_themeMode, PopupBorderHover) : ElaThemeColor(_themeMode, BasicBorder));
    painter.setBrush(ElaThemeColor(_themeMode, BasicBaseAlpha));
    painter.drawRoundedRect(foregroundRect, _pBorderRadius, _pBorderRadius);
    // 图片绘制
    painter.save();
    QRectF pixRect(
        foregroundRect.x() + foregroundRect.height() * 0.15,
        foregroundRect.y() + foregroundRect.height() * 0.15,
        foregroundRect.height() * 0.7,
        foregroundRect.height() * 0.7
    );
    QPainterPath pixPath;
    pixPath.addRoundedRect(pixRect, 4, 4);
    painter.setClipPath(pixPath);
    painter.drawPixmap(pixRect, _pCardPixmap, _pCardPixmap.rect());
    painter.restore();

    // 文字绘制
    // 计算按钮最终矩形
    int buttonTargetWidth = _floater->_overButton->fontMetrics().horizontalAdvance(_floater->_overButton->text()) + 40;
    if (buttonTargetWidth > width() * 0.25)
    {
        buttonTargetWidth = qRound(width() * 0.25);
    }
    // 处理ElaPushButton的阴影偏移
    _buttonTargetRect = QRect(
        QPoint(
            qRound(width() + 2 * _floater->_floatGeometryOffset - _shadowBorderWidth + 3 - foregroundRect.height() * 0.15 - buttonTargetWidth),
            qRound(foregroundRect.height() * 0.15 - 3)
        ),
        QSize(buttonTargetWidth, 36)
    );

    // Title
    painter.setPen(ElaThemeColor(_themeMode, BasicText));
    QFont font = painter.font();
    font.setWeight(QFont::Bold);
    font.setPixelSize(fontPixelSize + 2);
    painter.setFont(font);
    int titleHeight = painter.fontMetrics().height();
    QRectF titleRect(
        pixRect.right() + _textHSpacing,
        pixRect.y(),
        _floater->_floatGeometryOffset * 2 + foregroundRect.width() - pixRect.width() - _textHSpacing * 2 - foregroundRect.height() * 0.15 -
        buttonTargetWidth,
        titleHeight
    );
    QString titleText = painter.fontMetrics().elidedText(_pTitle, Qt::ElideRight, qMax(0, qRound(titleRect.width())));
    painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, titleText);

    // SubTitle
    font.setWeight(QFont::DemiBold);
    font.setPixelSize(fontPixelSize);
    painter.setFont(font);
    int subTitleHeight = painter.fontMetrics().height();
    QRectF subTitleRect(
        pixRect.right() + _textHSpacing,
        titleRect.bottom() + _textVSpacing,
        _floater->_floatGeometryOffset * 2 + foregroundRect.width() - pixRect.width() - _textHSpacing * 2 - foregroundRect.height() * 0.15 -
        buttonTargetWidth,
        subTitleHeight
    );
    QString subTitleText = painter.fontMetrics().elidedText(_pSubTitle, Qt::ElideRight, qMax(0, qRound(subTitleRect.width())));
    painter.drawText(subTitleRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, subTitleText);

    // InteractiveTips
    int tipWidth = painter.fontMetrics().horizontalAdvance(_pInteractiveTips);
    int tipHeight = painter.fontMetrics().height();
    if (!_pInteractiveTips.isEmpty())
    {
        font.setWeight(QFont::DemiBold);
        font.setPixelSize(fontPixelSize - 1);
        painter.setFont(font);
        // 覆盖背景绘制
        QRectF tipRect(
            foregroundRect.right() - _textHSpacing - tipWidth,
            foregroundRect.bottom() - _textHSpacing - tipHeight,
            foregroundRect.width() / 2 - _textHSpacing,
            tipHeight
        );
        painter.setPen(Qt::NoPen);
        painter.setBrush(ElaThemeColor(_themeMode, BasicBaseDeep));
        QRectF baseRect = tipRect;
        baseRect.setRight(tipRect.x() + tipWidth);
        baseRect.adjust(-7, -3, 4, 3);
        _interactiveTipsBaseRect = baseRect;
        painter.drawRoundedRect(baseRect, 6, 6);
        // 文字绘制
        painter.setPen(ElaThemeColor(_themeMode, BasicText));
        painter.drawText(tipRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, _pInteractiveTips);
    }
    else
    {
        QRectF tipRect(
            foregroundRect.right() - _textHSpacing - 50,
            foregroundRect.bottom() - _textHSpacing - tipHeight,
            foregroundRect.width() / 2 - _textHSpacing,
            tipHeight
        );
        tipRect.setRight(tipRect.x() + tipWidth);
        tipRect.adjust(-7, -3, 4, 3);
        _interactiveTipsBaseRect = tipRect;
    }
    painter.restore();
}

void ElaPopularCard::elaDeferred_setCardButtonText_3(const QString& cardButtonText)
{
    if (cardButtonText.isEmpty())
    {
        return;
    }
    _pCardButtonText = cardButtonText;
    _floater->_overButton->setText(_pCardButtonText);
    Q_EMIT pCardButtonTextChanged();
}

void ElaPopularCard::elaDeferred_setCardFloatArea_4(QWidget* floatArea)
{
    if (!floatArea || floatArea == this)
    {
        return;
    }
    _pCardFloatArea = floatArea;
    _floater->setParent(floatArea);
    Q_EMIT pCardFloatAreaChanged();
}

void ElaTableViewStyle::elaDeferred_drawControl_0(ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    // qDebug() << element << option->rect;
    switch (element)
    {
    case QStyle::CE_ShapedFrame:
    {
        // viewport视口外的其他区域背景
        QRect frameRect = option->rect;
        frameRect.adjust(1, 1, -1, -1);
        painter->save();
        painter->setRenderHints(QPainter::Antialiasing);
        painter->setPen(ElaThemeColor(_themeMode, PopupBorder));
        painter->setBrush(ElaThemeColor(_themeMode, BasicBaseAlpha));
        painter->drawRoundedRect(frameRect, 3, 3);
        painter->restore();
        return;
    }
    case QStyle::CE_HeaderLabel:
    {
        // 表头文字绘制
        if (const auto* hopt = qstyleoption_cast<const QStyleOptionHeader*>(option))
        {
            QRect headerRect = option->rect;
            painter->save();
            painter->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
            if (!hopt->text.isEmpty())
            {
                painter->setPen(ElaThemeColor(_themeMode, BasicText));
                painter->drawText(headerRect, hopt->textAlignment, hopt->text);
            }
            painter->restore();
        }
        return;
    }
    case QStyle::CE_HeaderSection:
    {
        // 表头背景绘制
        painter->save();
        painter->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(ElaThemeColor(_themeMode, BasicBaseDeepAlpha));
        painter->drawRect(option->rect);
        if (option->state.testFlag(QStyle::State_Sunken))
        {
            painter->setBrush(ElaThemeColor(_themeMode, BasicPressAlpha));
        }
        else
        {
            if (option->state.testFlag(QStyle::State_MouseOver))
            {
                painter->setBrush(ElaThemeColor(_themeMode, BasicHoverAlpha));
            }
        }
        painter->drawRect(option->rect);
        painter->restore();
        return;
    }
    case QStyle::CE_HeaderEmptyArea:
    {
        // 表头未使用区域背景绘制
        QRect frameRect = option->rect;
        painter->save();
        painter->setRenderHints(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(ElaThemeColor(_themeMode, BasicBaseDeep));
        painter->drawRect(frameRect);
        painter->restore();
        return;
    }
    case QStyle::CE_ItemViewItem:
    {
        if (const auto* vopt = qstyleoption_cast<const QStyleOptionViewItem*>(option))
        {
            // 背景绘制
            const auto* tabView = dynamic_cast<const ElaTableView*>(widget);
            if (!tabView)
            {
                return;
            }
            this->drawPrimitive(QStyle::PE_PanelItemViewItem, option, painter, widget);
            QAbstractItemView::SelectionBehavior selectionBehavior = tabView->selectionBehavior();
            // 内容绘制
            QRect itemRect = option->rect;
            painter->save();
            painter->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
            // QRect checkRect = proxy()->subElementRect(SE_ItemViewItemCheckIndicator, vopt, widget);
            QRect iconRect = proxy()->subElementRect(SE_ItemViewItemDecoration, vopt, widget);
            QRect textRect = proxy()->subElementRect(SE_ItemViewItemText, vopt, widget);
            if (vopt->index.column() == 0)
            {
                iconRect.adjust(_horizontalPadding, 0, _horizontalPadding, 0);
                textRect.adjust(_horizontalPadding, 0, 0, 0);
            }
            // 图标绘制
            if (!vopt->icon.isNull())
            {
                QIcon::Mode mode = QIcon::Normal;
                // if (!(vopt->state.testFlag(QStyle::State_Enabled)))
                // {
                //     mode = QIcon::Disabled;
                // }
                // else if (vopt->state.testFlag(QStyle::State_Selected))
                // {
                //     mode = QIcon::Selected;
                // }
                QIcon::State state = vopt->state & QStyle::State_Open ? QIcon::On : QIcon::Off;
                vopt->icon.paint(painter, iconRect, vopt->decorationAlignment, mode, state);
            }
            // 文字绘制
            if (!vopt->text.isEmpty())
            {
                painter->setPen(ElaThemeColor(_themeMode, BasicText));
                painter->drawText(textRect, vopt->displayAlignment, vopt->text);
            }
            // 选中特效
            int heightOffset = itemRect.height() / 4;
            painter->setPen(Qt::NoPen);
            painter->setBrush(ElaThemeColor(_themeMode, PrimaryNormal));
            if (vopt->state.testFlag(QStyle::State_Selected))
            {
                if (selectionBehavior == QAbstractItemView::SelectRows && vopt->index.column() == 0)
                {
                    painter->drawRoundedRect(QRectF(itemRect.x() + 3, itemRect.y() + heightOffset, 3, itemRect.height() - 2 * heightOffset), 3, 3);
                }
            }
            painter->restore();
        }
        return;
    }
    default:
    {
        break;
    }
    }
    QProxyStyle::drawControl(element, option, painter, widget);
}

void ElaTableViewStyle::elaDeferred_drawPrimitive_1(
    PrimitiveElement element,
    const QStyleOption* option,
    QPainter* painter,
    const QWidget* widget
) const
{
    // qDebug() << element << option->rect << widget->objectName();
    switch (element)
    {
    case QStyle::PE_PanelItemViewItem:
    {
        // 行覆盖绘制
        if (const auto* vopt = qstyleoption_cast<const QStyleOptionViewItem*>(option))
        {
            const auto* tabView = dynamic_cast<const ElaTableView*>(widget);
            if (!tabView)
            {
                return;
            }
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            QAbstractItemView::SelectionBehavior selectionBehavior = tabView->selectionBehavior();
            if (selectionBehavior == QAbstractItemView::SelectRows)
            {
                if (vopt->index.row() == _pCurrentHoverRow)
                {
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(ElaThemeColor(_themeMode, BasicHoverAlpha));
                    painter->drawRect(vopt->rect);
                }
            }
            else
            {
                if (vopt->state.testFlag(QStyle::State_Selected) || vopt->state.testFlag(QStyle::State_MouseOver))
                {
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(ElaThemeColor(_themeMode, BasicHoverAlpha));
                    painter->drawRect(vopt->rect);
                }
            }
            painter->restore();
        }
        return;
    }
    case QStyle::PE_PanelItemViewRow:
    {
        if (const auto* vopt = qstyleoption_cast<const QStyleOptionViewItem*>(option))
        {
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            QRect itemRect = vopt->rect;
            painter->setPen(Qt::NoPen);
            if (vopt->state & QStyle::State_Selected)
            {
                // 选中
                painter->setBrush(ElaThemeColor(_themeMode, BasicSelectedAlpha));
                painter->drawRect(itemRect);
            }
            else
            {
                if (vopt->features.testFlag(QStyleOptionViewItem::Alternate))
                {
                    // Item背景隔行变色
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(ElaThemeColor(_themeMode, BasicAlternating));
                    painter->drawRect(vopt->rect);
                }
            }
            painter->restore();
        }
        return;
    }
    case QStyle::PE_Widget:
    {
        return;
    }
    default:
    {
        break;
    }
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

void ElaApplication::elaDeferredInitMicaBaseImage(const QImage& img)
{
    if (img.isNull())
    {
        return;
    }
    auto* initThread = new QThread();
    auto* initObject = new ElaMicaBaseInitObject(this);
    connect(initThread, &QThread::finished, initObject, &ElaMicaBaseInitObject::deleteLater);
    connect(
        initObject,
        &ElaMicaBaseInitObject::initFinished,
        initThread,
        [=]() {
            Q_EMIT this->pWindowDisplayModeChanged();
            _updateAllMicaWidget();
            initThread->quit();
            initThread->wait();
            initThread->deleteLater();
        }
    );
    initObject->moveToThread(initThread);
    initThread->start();
    connect(this, &ElaApplication::initMicaBase, initObject, &ElaMicaBaseInitObject::onInitMicaBase);
    Q_EMIT initMicaBase(img);
}

void ElaDockWidget::elaDeferred_ElaDockWidget_0(QWidget* parent, Qt::WindowFlags flags)
{
    setObjectName("ElaDockWidget");
    _titleBar = new ElaDockWidgetTitleBar(this);
    setTitleBarWidget(_titleBar);

    // 主题变更动画
    _themeMode = eTheme->getThemeMode();
    connect(eTheme, &ElaTheme::themeModeChanged, this, &ElaDockWidget::onThemeModeChanged);

    _windowDisplayMode = eApp->getWindowDisplayMode();
    connect(
        eApp,
        &ElaApplication::pWindowDisplayModeChanged,
        this,
        [=]() {
            _windowDisplayMode = eApp->getWindowDisplayMode();
            update();
        }
    );
    connect(
        this,
        &ElaDockWidget::topLevelChanged,
        this,
        [=](bool topLevel) {
            if (eApp->getWindowDisplayMode() == ElaApplicationType::WindowDisplayMode::ElaMica)
            {
                eApp->syncWindowDisplayMode(this, topLevel);
            }
        }
    );
}

void ElaKeyBinderContainer::elaDeferred_ElaKeyBinderContainer_0(QWidget* parent)
{
    _keyBinder = dynamic_cast<ElaKeyBinder*>(parent);
    setStyleSheet("#ElaKeyBinderContainer{background-color:transparent;}");
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setFixedHeight(140);
    QFont textFont = font();
    textFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.5);
    textFont.setPixelSize(eApp->getFontPixelSize() + 3);
    setFont(textFont);
    _themeMode = eTheme->getThemeMode();
    connect(
        eTheme,
        &ElaTheme::themeModeChanged,
        this,
        [=](ElaThemeType::ThemeMode themeMode) {
            _themeMode = themeMode;
            update();
        }
    );
}

void ElaKeyBinderContainer::elaDeferred_saveBinderChanged_1() const
{
    Q_EMIT _keyBinder->binderKeyTextChanged(_pBinderKeyText);
    Q_EMIT _keyBinder->nativeVirtualBinderKeyChanged(_pNativeVirtualBinderKey);
    if (_pBinderKeyText.isEmpty())
    {
        _keyBinder->setText(u8"  按键: " + QString(u8"未绑定") + "      ");
    }
    else
    {
        _keyBinder->setText(u8"  按键: " + _pBinderKeyText + "      ");
    }
}

void ElaMenuStyle::elaDeferred_drawControl_0(ControlElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const
{
    switch (element)
    {
    case QStyle::CE_MenuItem:
    {
        // 内容绘制 区分类型
        if (const auto* mopt = qstyleoption_cast<const QStyleOptionMenuItem*>(option))
        {
            if (mopt->menuItemType == QStyleOptionMenuItem::Separator)
            {
                QRect separatorRect = mopt->rect;
                painter->save();
                painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
                painter->setPen(Qt::NoPen);
                painter->setBrush(ElaThemeColor(_themeMode, BasicBaseLine));
                painter->drawRoundedRect(
                    QRectF(
                        separatorRect.x() + separatorRect.width() * 0.055,
                        separatorRect.center().y(),
                        separatorRect.width() - separatorRect.width() * 0.11,
                        1.5
                    ),
                    1,
                    1
                );
                painter->restore();
                return;
            }
            else
            {
                QRect menuRect = mopt->rect;
                qreal contentPadding = menuRect.width() * 0.055;
                painter->save();
                painter->setRenderHints(QPainter::SmoothPixmapTransform | QPainter::Antialiasing | QPainter::TextAntialiasing);
                // 覆盖效果
                if (mopt->state.testFlag(QStyle::State_Enabled) && (mopt->state.testFlag(QStyle::State_MouseOver) || mopt->state.testFlag(
                    QStyle::State_Selected
                )))
                {
                    QRect hoverRect = menuRect;
                    hoverRect.adjust(0, 2, 0, -2);
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(ElaThemeColor(_themeMode, PopupHover));
                    painter->drawRoundedRect(hoverRect, 5, 5);
                }
                // Icon绘制
                QIcon menuIcon = mopt->icon;
                // check绘制
                if (mopt->menuHasCheckableItems)
                {
                    painter->save();
                    painter->setPen(
                        !mopt->state.testFlag(QStyle::State_Enabled) ? Qt::gray : _themeMode == ElaThemeType::Light ? Qt::black : Qt::white
                    );
                    QFont iconFont;
            iconFont.setFamily(QStringLiteral("ElaAwesome"));
                    iconFont.setPixelSize(qRound(_pMenuItemHeight * 0.57));
                    painter->setFont(iconFont);
                    painter->drawText(
                        QRectF(menuRect.x() + contentPadding, menuRect.y(), _iconWidth, menuRect.height()),
                        Qt::AlignCenter,
                        mopt->checked ? ElaIconChar(ElaIconType::Check) : ElaIconChar(ElaIconType::None)
                    );
                    painter->restore();
                }
                else
                {
                    QString iconText;
                    if (const auto* menu = dynamic_cast<const ElaMenu*>(widget))
                    {
                        if (QAction* action = menu->actionAt(menuRect.center()))
                        {
                            iconText = action->property("ElaIconType").toString();
                        }
                    }
                    if (!iconText.isEmpty())
                    {
                        painter->save();
                        painter->setPen(
                            !mopt->state.testFlag(QStyle::State_Enabled) ? Qt::gray : _themeMode == ElaThemeType::Light ? Qt::black : Qt::white
                        );
                        QFont iconFont;
            iconFont.setFamily(QStringLiteral("ElaAwesome"));
                        iconFont.setPixelSize(qRound(_pMenuItemHeight * 0.57));
                        painter->setFont(iconFont);
                        painter->drawText(
                            QRectF(menuRect.x() + contentPadding, menuRect.y(), _iconWidth, menuRect.height()),
                            Qt::AlignCenter,
                            iconText
                        );
                        painter->restore();
                    }
                    else
                    {
                        if (!menuIcon.isNull())
                        {
                            painter->drawPixmap(
                                QRect(
                                    static_cast<int>(menuRect.x() + contentPadding),
                                    menuRect.center().y() - _iconWidth / 2,
                                    _iconWidth,
                                    _iconWidth
                                ),
                                menuIcon.pixmap(_iconWidth, _iconWidth)
                            );
                        }
                    }
                }
                // 文字和快捷键绘制
                if (!mopt->text.isEmpty())
                {
                    qreal textLeftSpacing = 8;
                    QStringList textList = mopt->text.split("\t");
                    painter->setPen(
                        !mopt->state.testFlag(QStyle::State_Enabled) ? Qt::gray : _themeMode == ElaThemeType::Light ? Qt::black : Qt::white
                    );

                    painter->drawText(
                        QRectF(
                            menuRect.x() + (_isAnyoneItemHasIcon ? contentPadding + textLeftSpacing : 0) + _iconWidth,
                            menuRect.y(),
                            menuRect.width(),
                            menuRect.height()
                        ),
                        Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                        textList[0]
                    );
                    if (textList.count() > 1)
                    {
                        painter->drawText(
                            QRectF(
                                menuRect.x() + contentPadding + _iconWidth + textLeftSpacing,
                                menuRect.y(),
                                menuRect.width() - (contentPadding * 2 + _iconWidth + textLeftSpacing),
                                menuRect.height()
                            ),
                            Qt::AlignRight | Qt::AlignVCenter | Qt::TextSingleLine,
                            textList[1]
                        );
                    }
                }
                // 展开图标
                if (mopt->menuItemType == QStyleOptionMenuItem::SubMenu)
                {
                    painter->save();
                    painter->setPen(
                        !mopt->state.testFlag(QStyle::State_Enabled) ? Qt::gray : _themeMode == ElaThemeType::Light ? Qt::black : Qt::white
                    );
                    QFont iconFont;
            iconFont.setFamily(QStringLiteral("ElaAwesome"));
                    iconFont.setPixelSize(eApp->getFontPixelSize() + 5);
                    painter->setFont(iconFont);
                    painter->drawText(
                        QRect(menuRect.right() - 25, menuRect.y(), 25, menuRect.height()),
                        Qt::AlignVCenter,
                        ElaIconChar(ElaIconType::AngleRight)
                    );
                    painter->restore();
                }
                painter->restore();
            }
        }

        return;
    }
    case QStyle::CE_MenuEmptyArea:
    {
        return;
    }
    default:
    {
        break;
    }
    }
    QProxyStyle::drawControl(element, option, painter, widget);
}

QSize ElaMenuStyle::elaDeferred_sizeFromContents_1(ContentsType type, const QStyleOption* option, const QSize& size, const QWidget* widget) const
{
    switch (type)
    {
    case QStyle::CT_MenuItem:
    {
        if (const auto* mopt = qstyleoption_cast<const QStyleOptionMenuItem*>(option))
        {
            if (mopt->menuItemType == QStyleOptionMenuItem::Separator)
            {
                break;
            }
            QSize menuItemSize = QProxyStyle::sizeFromContents(type, option, size, widget);
            const auto* menu = dynamic_cast<const ElaMenu*>(widget);
            if (menu->isHasIcon() || mopt->menuHasCheckableItems)
            {
                _isAnyoneItemHasIcon = true;
            }
            if (menu->isHasChildMenu())
            {
                return QSize(menuItemSize.width() + 20, _pMenuItemHeight);
            }
            else
            {
                return QSize(menuItemSize.width(), _pMenuItemHeight);
            }
        }
    }
    default:
    {
        break;
    }
    }
    return QProxyStyle::sizeFromContents(type, option, size, widget);
}

void ElaMenuBar::elaDeferred_ElaMenuBar_0(QWidget* parent)
{
    setMouseTracking(true);
    setObjectName("ElaMenuBar");
    setStyle(new ElaMenuBarStyle(style()));
    auto* tool = this->findChild<QToolButton*>();
    if (tool->objectName() == "qt_menubar_ext_button")
    {
        QMenu* oldMenu = tool->menu();
        auto* menu = new ElaMenu(this);
        menu->setObjectName("ElaExtendMenu");
        menu->setMenuItemHeight(27);
        if (oldMenu)
        {
            oldMenu->deleteLater();
        }
        tool->setMenu(menu);
    }
}

void ElaScrollBar::elaDeferred_ElaScrollBar_0(QWidget* parent)
{
    setSingleStep(1);
    setObjectName("ElaScrollBar");
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    _pSpeedLimit = 20;
    _pTargetMaximum = 0;
    _pIsAnimation = false;
    connect(this, &ElaScrollBar::rangeChanged, this, &ElaScrollBar::onRangeChanged);
    auto* scrollBarStyle = new ElaScrollBarStyle(style());
    scrollBarStyle->setScrollBar(this);
    setStyle(scrollBarStyle);
    _slideSmoothAnimation = new QPropertyAnimation(this, "value");
    _slideSmoothAnimation->setEasingCurve(QEasingCurve::OutSine);
    _slideSmoothAnimation->setDuration(300);
    connect(_slideSmoothAnimation, &QPropertyAnimation::finished, this, [=]() { _scrollValue = value(); });

    _expandTimer = new QTimer(this);
    connect(
        _expandTimer,
        &QTimer::timeout,
        this,
        [=]() {
            _expandTimer->stop();
            _isExpand = underMouse();
            scrollBarStyle->startExpandAnimation(_isExpand);
        }
    );
}

void ElaActionCommanderView::elaDeferred_onCustomContextMenuRequested_0(const QPoint& pos)
{
    auto modelIndex = indexAt(pos);
    auto actionCommander = ElaActionCommander::getInstance();
    ElaMenu menu(this);
    menu.setMinimumWidth(200);
    menu.setMenuItemHeight(35);
    connect(
        menu.addElaIconAction(ElaIconType::LocationArrow, "定位到当前位置"),
        &QAction::triggered,
        this,
        [=]() {
            auto selectedIndexList = selectionModel()->selectedRows(0);
            if (selectedIndexList.isEmpty())
            {
                return;
            }
            auto vScrollBar = verticalScrollBar();
            int startValue = vScrollBar->value();
            scrollTo(selectedIndexList[0], QAbstractItemView::PositionAtCenter);
            int endValue = vScrollBar->value();
            auto* scrollAnimation = new QPropertyAnimation(vScrollBar, "value");
            scrollAnimation->setEasingCurve(QEasingCurve::OutSine);
            scrollAnimation->setDuration(255);
            scrollAnimation->setStartValue(startValue);
            scrollAnimation->setEndValue(endValue);
            scrollAnimation->start(QAbstractAnimation::DeleteWhenStopped);
        }
    );
    connect(menu.addElaIconAction(ElaIconType::ListTree, "展开全部"), &QAction::triggered, this, [=]() { expandAll(); });
    connect(menu.addElaIconAction(ElaIconType::ListUl, "收起全部"), &QAction::triggered, this, [=]() { collapseAll(); });
    auto undoAction = menu.addElaIconAction(ElaIconType::RotateLeft, "撤销");
    undoAction->setEnabled(actionCommander->getCommanderUndoState(_pDomainName) == ElaActionCommanderType::UndoValid);
    connect(undoAction, &QAction::triggered, this, [=]() { actionCommander->undoCommand(_pDomainName); });
    auto redoAction = menu.addElaIconAction(ElaIconType::RotateRight, "重做");
    redoAction->setEnabled(actionCommander->getCommanderRedoState(_pDomainName) == ElaActionCommanderType::RedoValid);
    connect(redoAction, &QAction::triggered, this, [=]() { actionCommander->redoCommand(_pDomainName); });
    if (modelIndex.isValid())
    {
        // 如果是次要行 定位到主要行
        if (modelIndex.parent().isValid())
        {
            modelIndex = modelIndex.parent();
        }
        connect(
            menu.addElaIconAction(ElaIconType::ArrowsRotate, "运行到此处"),
            &QAction::triggered,
            this,
            [=]() { Q_EMIT runToCommand(_pDomainName, modelIndex.row()); }
        );
    }
    menu.exec(QCursor::pos());
}

void ElaNavigationView::elaDeferredDoCompactToolTip()
{
    if (_pNavigationBarPrivate->_currentDisplayMode == ElaNavigationType::NavigationDisplayMode::Compact)
    {
        if (!_compactToolTip)
        {
            _compactToolTip = new ElaToolTip(this);
        }
        QModelIndex posIndex = indexAt(mapFromGlobal(QCursor::pos()));
        if (!posIndex.isValid())
        {
            _compactToolTip->hide();
            return;
        }
        auto* posNode = static_cast<ElaNavigationNode*>(posIndex.internalPointer());
        _compactToolTip->setToolTip(posNode->getNodeTitle());
        _compactToolTip->updatePos();
        _compactToolTip->show();
    }
    else
    {
        if (_compactToolTip)
        {
            _compactToolTip->deleteLater();
            _compactToolTip = nullptr;
        }
    }
}

void ElaNavigationView::elaDeferred_onCustomContextMenuRequested_1(const QPoint& pos)
{
    if (!_pNavigationBarPrivate->_pIsAllowPageOpenInNewWindow)
    {
        return;
    }
    QModelIndex posIndex = indexAt(pos);
    if (!posIndex.isValid())
    {
        return;
    }
    auto* posNode = static_cast<ElaNavigationNode*>(posIndex.internalPointer());
    if (!posNode->getIsExpanderNode() && !posNode->getIsCategoryNode())
    {
        ElaMenu menu;
        menu.setMenuItemHeight(27);
        QAction* openAction = menu.addElaIconAction(ElaIconType::ObjectGroup, "在新窗口中打开");
        connect(openAction, &QAction::triggered, this, [=]() { Q_EMIT navigationOpenNewWindow(posNode->getNodeKey()); });
        menu.exec(mapToGlobal(pos));
    }
}

void ElaRibbonBarView::elaDeferred_ElaRibbonBarView_0(QWidget* parent)
{
    _ribbonBar = dynamic_cast<ElaRibbonBar*>(parent);
    _pRibbonBarModel = nullptr;
    _pRibbonBarDelegate = nullptr;
    setObjectName("ElaRibbonBarView");
    setStyleSheet("ElaRibbonBarView{background-color: transparent;border:0px;}" "ElaRibbonBarView::item{border:none;}");
    setAutoScroll(false);
    setMouseTracking(true);
    setVerticalScrollBar(new ElaScrollBar(this));
    setHorizontalScrollBar(new ElaScrollBar(this));
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setFlow(QListView::LeftToRight);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    QScroller::grabGesture(viewport(), QScroller::LeftMouseButtonGesture);
    QScroller* scroller = QScroller::scroller(viewport());
    QScrollerProperties properties = scroller->scrollerProperties();
    properties.setScrollMetric(QScrollerProperties::MousePressEventDelay, 0);
    properties.setScrollMetric(QScrollerProperties::HorizontalOvershootPolicy, QScrollerProperties::OvershootAlwaysOn);
    properties.setScrollMetric(QScrollerProperties::OvershootDragResistanceFactor, 0.35);
    properties.setScrollMetric(QScrollerProperties::OvershootScrollTime, 0.5);
    properties.setScrollMetric(QScrollerProperties::FrameRate, QScrollerProperties::Fps60);
    scroller->setScrollerProperties(properties);
    connect(
        scroller,
        &QScroller::stateChanged,
        this,
        [=](QScroller::State newState) {
            if (!_pRibbonBarDelegate)
            {
                return;
            }
            if (newState == QScroller::Pressed)
            {
                _pRibbonBarDelegate->setPressIndex(indexAt(mapFromGlobal(QCursor::pos())));
                viewport()->update();
            }
            else if (newState == QScroller::Scrolling || newState == QScroller::Inactive)
            {
                _pRibbonBarDelegate->setPressIndex(QModelIndex());
            }
        }
    );

    _themeMode = eTheme->getThemeMode();
    connect(eTheme, &ElaTheme::themeModeChanged, this, [=](ElaThemeType::ThemeMode themeMode) { _themeMode = themeMode; });
}

void ElaRibbonBarView::elaDeferred_mouseReleaseEvent_1(QMouseEvent* event)
{
    QListView::mouseReleaseEvent(event);
    _pRibbonBarDelegate->setPressIndex(QModelIndex{});
    viewport()->update();
    // 触发Action
    auto modelIndex = indexAt(event->pos());
    if (modelIndex.isValid())
    {
        auto action = _pRibbonBarModel->getRibbonAction(modelIndex.row());
        if (!action || !action->isEnabled())
        {
            return;
        }
        auto actionMenu = action->menu();
        auto groupName = action->property("ElaRibbonGroupName").toString();
        action->trigger();
        Q_EMIT _ribbonBar->ribbonActionTriggered(_ribbonBar->getCurrentRibbonPage(), groupName, action);
        if (actionMenu)
        {
            QRect menuRect = actionMenu->rect();
            QRect indexRect = visualRect(modelIndex);
            if (groupName.isEmpty())
            {
                actionMenu->exec(mapToGlobal(QPoint(indexRect.center().x() - menuRect.width() / 2, indexRect.bottom() - 5)));
            }
            else
            {
                actionMenu->exec(
                    mapToGlobal(
                        QPoint(indexRect.center().x() - menuRect.width() / 2, indexRect.bottom() - _pRibbonBarDelegate->getBottomMargin() - 5)
                    )
                );
            }
        }
    }
}

void ElaScrollPage::elaDeferred_ElaScrollPage_0(QWidget* parent)
{
    setProperty("ElaBaseClassName", "ElaScrollPage");
    _breadcrumbBar = new ElaBreadcrumbBar(this);
    _breadcrumbBar->setTextPixelSize(28);
    connect(
        _breadcrumbBar,
        &ElaBreadcrumbBar::breadcrumbClicked,
        this,
        [=](const QString& breadcrumb, const QStringList& lastBreadcrumbList) {
            if (_centralWidgetMap.contains(breadcrumb))
            {
                auto command = new ElaScrollPageRouteCommand(this);
                command->setIsBreadcrumbClicked(true);
                command->setScrollPagePrivate(this);
                command->setUndoBreadcrumbList(lastBreadcrumbList);
                command->setRedoBreadcrumbList(_breadcrumbBar->getBreadcrumbList());
                ElaActionCommander::getInstance()->recordCommand("ElaWidgetToolsAction", command);
            }
        }
    );
    _pageTitleLayout = new QHBoxLayout();
    _pageTitleLayout->setContentsMargins(0, 0, 0, 0);
    _pageTitleLayout->addWidget(_breadcrumbBar);

    _centralStackedWidget = new QStackedWidget(this);
    _centralStackedWidget->setContentsMargins(0, 0, 0, 0);

    _mainLayout = new QVBoxLayout(this);
    _mainLayout->setSpacing(0);
    _mainLayout->setContentsMargins(0, 0, 0, 0);
    _mainLayout->addLayout(_pageTitleLayout);
    _topCustomWidget = new QWidget(this);
    _topCustomWidget->setVisible(false);
    _mainLayout->addWidget(_topCustomWidget);
    _mainLayout->addWidget(_centralStackedWidget);
    _bottomCustomWidget = new QWidget(this);
    _bottomCustomWidget->setVisible(false);
    _mainLayout->addWidget(_bottomCustomWidget);
    setContentsMargins(20, 20, 0, 0);
}

void ElaScrollPage::elaDeferred_navigation_1(int widgetIndex, bool isLogRoute)
{
    if (widgetIndex >= _centralStackedWidget->count() || _navigationTargetIndex == widgetIndex)
    {
        return;
    }
    int currentIndex = _navigationTargetIndex;
    _switchCentralStackIndex(widgetIndex, _navigationTargetIndex);
    _navigationTargetIndex = widgetIndex;
    QString pageTitle = _centralWidgetMap.key(widgetIndex);
    if (isLogRoute)
    {
        auto command = new ElaScrollPageRouteCommand(this);
        command->setIsBreadcrumbClicked(false);
        command->setScrollPagePrivate(this);
        command->setUndoPageIndex(currentIndex);
        command->setRedoPageIndex(widgetIndex);
        ElaActionCommander::getInstance()->recordCommand("ElaWidgetToolsAction", command, false);
    }
    _breadcrumbBar->appendBreadcrumb(pageTitle);
}

void ElaToolBarStyle::elaDeferred_drawPrimitive_0(
    PrimitiveElement element,
    const QStyleOption* option,
    QPainter* painter,
    const QWidget* widget
) const
{
    switch (element)
    {
    case QStyle::PE_PanelButtonTool:
    {
        return;
    }
    case QStyle::PE_FrameMenu:
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(ElaThemeColor(_themeMode, PopupBorder));
        painter->setBrush(ElaThemeColor(_themeMode, DialogBase));
        painter->drawRect(option->rect);
        painter->restore();
        return;
    }
    case QStyle::PE_IndicatorToolBarHandle:
    {
        // 拖动虚线
        QRect handleRect = option->rect;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QPen handlePen(Qt::DotLine);
        handlePen.setColor(ElaThemeColor(_themeMode, PrimaryNormal));
        handlePen.setWidthF(1.8);
        painter->setPen(handlePen);
        const auto* toolBar = dynamic_cast<const ElaToolBar*>(widget);
        if (toolBar->orientation() == Qt::Vertical)
        {
            painter->drawLine(handleRect.x(), handleRect.center().y(), handleRect.right(), handleRect.center().y());
        }
        else
        {
            painter->drawLine(handleRect.center().x(), handleRect.y(), handleRect.center().x(), handleRect.bottom());
        }
        painter->restore();
        return;
    }
    case QStyle::PE_IndicatorToolBarSeparator:
    {
        // 间隔符绘制
        QRect separatorRect = option->rect;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(ElaThemeColor(_themeMode, PrimaryNormal));
        const auto* toolBar = dynamic_cast<const ElaToolBar*>(widget);
        if (toolBar->orientation() == Qt::Vertical)
        {
            painter->drawRect(
                QRectF(separatorRect.x() + separatorRect.width() * 0.1, separatorRect.center().y() - 0.9, separatorRect.width() * 0.8, 1.8)
            );
        }
        else
        {
            painter->drawRect(
                QRectF(separatorRect.center().x() - 0.9, separatorRect.y() + separatorRect.height() * 0.1, 1.8, separatorRect.height() * 0.8)
            );
        }
        painter->restore();
        return;
    }
    default:
    {
        break;
    }
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

void ElaCustomTabWidget::elaDeferred_ElaCustomTabWidget_0(QWidget* parent)
{
    _pIsFinished = false;
    resize(700, 500);
    setWindowTitle("");
#ifndef Q_OS_WIN
    setAttribute(Qt::WA_Hover);
#endif
    setMouseTracking(true);
    setWindowIcon(QIcon());
    _customTabWidget = new ElaTabWidget(this);
    _customTabWidget->setIsTabTransparent(true);
    _customTabWidget->setObjectName("ElaCustomTabWidget");
    QTabBar* originTabBar = _customTabWidget->tabBar();
    originTabBar->hide();
    setAcceptDrops(true);
    _customTabBar = new ElaTabBar(this);
    _customTabBar->setObjectName("ElaCustomTabBar");
    connect(_customTabBar, &ElaTabBar::tabMoved, this, [=](int from, int to) { _customTabWidget->tabBar()->moveTab(from, to); });
    connect(_customTabBar, &ElaTabBar::currentChanged, this, [=](int index) { _customTabWidget->setCurrentIndex(index); });
    connect(
        _customTabWidget,
        &ElaTabWidget::currentChanged,
        this,
        [=](int index) {
            if (index == -1)
            {
                _pIsFinished = true;
                hide();
            }
        }
    );
    connect(_customTabBar, &ElaTabBar::tabCloseRequested, originTabBar, &QTabBar::tabCloseRequested);
    connect(
        _customTabBar,
        &ElaTabBar::tabBarTextChanged,
        originTabBar,
        [=](int tabIndex, const QString& tabText) { originTabBar->setTabText(tabIndex, tabText); }
    );

    _customTabWidget->_customTabBar = _customTabBar;
    connect(_customTabBar, &ElaTabBar::tabDragCreate, _customTabWidget, &ElaTabWidget::onTabDragCreate);
    connect(_customTabBar, &ElaTabBar::tabDragDrop, _customTabWidget, &ElaTabWidget::onTabDragDrop);
    connect(_customTabBar, &ElaTabBar::tabDragEnter, _customTabWidget, &ElaTabWidget::onTabDragEnter);
    connect(_customTabBar, &ElaTabBar::tabDragLeave, _customTabWidget, &ElaTabWidget::onTabDragLeave);
    auto* customWidget = new QWidget(this);
    auto* customLayout = new QVBoxLayout(customWidget);
    customLayout->setContentsMargins(10, 0, 10, 0);
    customLayout->addStretch();
    customLayout->addWidget(_customTabBar);
    _appBar->setCustomWidget(ElaAppBarType::LeftArea, customWidget, this, "processHitTest");
    setCentralWidget(_customTabWidget);
}

void ElaCustomTabWidget::elaDeferred_addTab_1(QWidget* widget, const QIcon& tabIcon, const QString& tabTitle) const
{
    _customTabBar->addTab(tabIcon, tabTitle);
    _customTabWidget->addTab(widget, tabIcon, tabTitle);
}

void ElaCustomTabWidget::elaDeferred_ElaCustomTabWidget_2() const
{
    while (_customTabWidget->count() > 0)
    {
        QWidget* closeWidget = _customTabWidget->widget(0);
        if (auto* originTabWidget = closeWidget->property("ElaOriginTabWidget").value<ElaTabWidget*>())
        {
            closeWidget->setProperty("CurrentCustomBar", QVariant::fromValue<ElaTabBar*>(nullptr));
            originTabWidget->addTab(closeWidget, _customTabWidget->tabIcon(0), _customTabWidget->tabText(0));
            originTabWidget->setCurrentWidget(closeWidget);
        }
        else
        {
            _customTabWidget->removeTab(0);
        }
    }
}

void ElaNavigationBar::elaDeferred_onFooterViewClicked_0(const QModelIndex& index, bool isLogRoute, bool isRouteBack)
{
    auto* node = index.data(Qt::UserRole).value<ElaNavigationNode*>();
    if (node->getKeyPoints())
    {
        node->setKeyPoints(0);
        _navigationView->update();
    }
    ElaNavigationNode* selectedNode = _footerModel->getSelectedNode();

    if (selectedNode != node)
    {
        // 记录跳转
        if (isLogRoute && node->getIsHasFooterPage())
        {
            QString backPageKey;
            if (selectedNode)
            {
                backPageKey = selectedNode->getNodeKey();
            }
            else
            {
                if (_navigationModel->getSelectedNode())
                {
                    backPageKey = _navigationModel->getSelectedNode()->getNodeKey();
                }
            }
            auto* command = new ElaNavigationRouteCommand(this);
            command->setNavigationBar(this);
            command->setUndoPageKey(backPageKey);
            command->setRedoPageKey(node->getNodeKey());
            ElaActionCommander::getInstance()->recordCommand("ElaWidgetToolsAction", command, false);
        }
        Q_EMIT this->navigationNodeClicked(ElaNavigationType::FooterNode, node->getNodeKey(), isRouteBack);

        if (node->getIsHasFooterPage())
        {
            if (_navigationModel->getSelectedNode() || _navigationModel->getSelectedExpandedNode())
            {
                QVariantMap mainPostData = QVariantMap();
                mainPostData.insert("SelectMarkChanged", true);
                mainPostData.insert(
                    "LastSelectedNode",
                    QVariant::fromValue(
                        _navigationModel->getSelectedExpandedNode()
                            ? _navigationModel->getSelectedExpandedNode()
                            : _navigationModel->getSelectedNode()
                    )
                );
                mainPostData.insert("SelectedNode", QVariant::fromValue(nullptr));
                _navigationView->clearSelection();
                _navigationView->navigationNodeStateChange(mainPostData);
                _navigationModel->setSelectedExpandedNode(nullptr);
                _navigationModel->setSelectedNode(nullptr);
            }
            _footerView->clearSelection();
            _footerView->selectionModel()->select(index, QItemSelectionModel::Select);
            QVariantMap postData = QVariantMap();
            postData.insert("SelectMarkChanged", true);
            postData.insert("LastSelectedNode", QVariant::fromValue(_footerModel->getSelectedNode()));
            postData.insert("SelectedNode", QVariant::fromValue(node));
            _footerDelegate->navigationNodeStateChange(postData);
            _footerModel->setSelectedNode(node);
        }
    }
}

void ElaNavigationBar::elaDeferred_onTreeViewClicked_1(const QModelIndex& index, bool isLogRoute, bool isRouteBack)
{
    if (index.isValid())
    {
        auto* node = static_cast<ElaNavigationNode*>(index.internalPointer());
        if (!node)
        {
            return;
        }
        if (node->getIsCategoryNode())
        {
            Q_EMIT this->navigationNodeClicked(ElaNavigationType::CategoryNode, node->getNodeKey(), isRouteBack);
            return;
        }
        if (node->getIsExpanderNode())
        {
            _expandOrCollapseExpanderNode(node, !_navigationView->isExpanded(index));
        }
        else
        {
            if (node->getKeyPoints())
            {
                node->setKeyPoints(0);
                _navigationView->update();
            }
            ElaNavigationNode* selectedNode = _navigationModel->getSelectedNode();
            if (selectedNode != node)
            {
                // 记录跳转
                if (isLogRoute)
                {
                    QString backPageKey;
                    if (selectedNode)
                    {
                        backPageKey = selectedNode->getNodeKey();
                    }
                    else
                    {
                        if (_footerModel->getSelectedNode())
                        {
                            backPageKey = _footerModel->getSelectedNode()->getNodeKey();
                        }
                    }
                    auto* command = new ElaNavigationRouteCommand(this);
                    command->setNavigationBar(this);
                    command->setUndoPageKey(backPageKey);
                    command->setRedoPageKey(node->getNodeKey());
                    ElaActionCommander::getInstance()->recordCommand("ElaWidgetToolsAction", command, false);
                }
                Q_EMIT this->navigationNodeClicked(ElaNavigationType::PageNode, node->getNodeKey(), isRouteBack);

                if (_footerModel->getSelectedNode())
                {
                    _footerView->clearSelection();
                    QVariantMap footerPostData = QVariantMap();
                    footerPostData.insert("SelectMarkChanged", true);
                    footerPostData.insert("LastSelectedNode", QVariant::fromValue(_footerModel->getSelectedNode()));
                    footerPostData.insert("SelectedNode", QVariant::fromValue(nullptr));
                    _footerModel->setSelectedNode(nullptr);
                    _footerDelegate->navigationNodeStateChange(footerPostData);
                }
                QVariantMap postData = QVariantMap();
                postData.insert("SelectMarkChanged", true);
                if (_navigationModel->getSelectedExpandedNode())
                {
                    postData.insert("LastSelectedNode", QVariant::fromValue(_navigationModel->getSelectedExpandedNode()));
                }
                else
                {
                    postData.insert("LastSelectedNode", QVariant::fromValue(_navigationModel->getSelectedNode()));
                }
                if (_currentDisplayMode == ElaNavigationType::Compact)
                {
                    ElaNavigationNode* originNode = node->getOriginalNode();
                    if (originNode == node)
                    {
                        postData.insert("SelectedNode", QVariant::fromValue(node));
                    }
                    else
                    {
                        if (originNode == _navigationModel->getSelectedExpandedNode())
                        {
                            _navigationModel->setSelectedNode(node);
                            _resetNodeSelected();
                            return;
                        }
                        _navigationModel->setSelectedExpandedNode(originNode);
                        postData.insert("SelectedNode", QVariant::fromValue(originNode));
                    }
                }
                else
                {
                    postData.insert("SelectedNode", QVariant::fromValue(node));
                }
                _navigationModel->setSelectedNode(node);
                _navigationView->navigationNodeStateChange(postData);
                if (!node->getIsVisible() && _currentDisplayMode != ElaNavigationType::Compact)
                {
                    _expandSelectedNodeParent();
                }
            }
        }
        _resetNodeSelected();
    }
}

// The QObject parent owns the command until recordCommand reparents it.
// NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks)
void ElaWindow::elaDeferred_setCurrentStackIndex_0(int currentStackIndex)
{
    if (currentStackIndex >= _centerStackedWidget->getContainerStackedWidget()->count() || currentStackIndex < 0 || currentStackIndex ==
        _centerStackedWidget->getLastTargetIndex())
    {
        return;
    }
    int currentCenterStackedWidgetIndex = _centerStackedWidget->getContainerStackedWidget()->currentIndex();
    auto command = new ElaWindowStackChangeCommand(this);
    command->setWindowPrivate(this);
    command->setUndoStackIndex(currentCenterStackedWidgetIndex);
    command->setRedoStackIndex(currentStackIndex);
    ElaActionCommander::getInstance()->recordCommand("ElaWidgetToolsAction", command);
}

// NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)
