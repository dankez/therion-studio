#include "MainWindow.h"

#include "text_editor/TextEditorTab.h"
#include "text_editor/map_editor/MapEditorTab.h"

#include <QAction>

void MainWindow::refreshViewMenuActions()
{
    if (sidebarCollapseAction_ != nullptr) {
        sidebarCollapseAction_->setText(isSidebarEffectivelyCollapsed() ? tr("Expand Sidebar") : tr("Collapse Sidebar"));
        sidebarCollapseAction_->setEnabled(sidebarContainer_ != nullptr);
    }

    if (rightPanelCollapseAction_ != nullptr) {
        const QString rightPanelLabel = currentDocumentRightPanelLabel();
        rightPanelCollapseAction_->setText(currentDocumentRightPanelCollapsed()
                                               ? tr("Expand %1").arg(rightPanelLabel)
                                               : tr("Collapse %1").arg(rightPanelLabel));
        rightPanelCollapseAction_->setEnabled(currentDocumentHasRightPanel());
    }

    if (contextHelpCollapseAction_ != nullptr) {
        const bool visible = currentDetachedMapTabWithContextHelp() != nullptr;
        contextHelpCollapseAction_->setVisible(visible);
        contextHelpCollapseAction_->setEnabled(visible);
        contextHelpCollapseAction_->setText(currentDetachedMapContextHelpCollapsed()
                                                ? tr("Expand Help")
                                                : tr("Collapse Help"));
    }

    if (mapMagnifierAction_ != nullptr) {
        const bool enabled = mapMagnifierAction_->data().toBool();
        mapMagnifierAction_->setText(enabled ? tr("Hide Map Magnifier") : tr("Show Map Magnifier"));
    }

    if (wallDirectionCheckAction_ != nullptr) {
        const bool enabled = wallDirectionCheckAction_->data().toBool();
        wallDirectionCheckAction_->setText(enabled ? tr("Hide Wall Direction Check") : tr("Check Wall Directions"));
    }

    refreshFullScreenAction();
}

void MainWindow::refreshFullScreenAction()
{
    if (fullScreenAction_ == nullptr) {
        return;
    }

    fullScreenAction_->setText(isFullScreen() ? tr("Exit Full Screen") : tr("Enter Full Screen"));
}

bool MainWindow::currentDocumentHasRightPanel() const
{
    if (auto *mapTab = qobject_cast<TherionStudio::MapEditorTab *>(currentDocumentWidget())) {
        return mapTab->hasRightPanel();
    }
    if (auto *textTab = qobject_cast<TherionStudio::TextEditorTab *>(currentDocumentWidget())) {
        return textTab->hasRightPanel();
    }
    return false;
}

bool MainWindow::currentDocumentRightPanelCollapsed() const
{
    if (auto *mapTab = qobject_cast<TherionStudio::MapEditorTab *>(currentDocumentWidget())) {
        return mapTab->isRightPanelCollapsed();
    }
    if (auto *textTab = qobject_cast<TherionStudio::TextEditorTab *>(currentDocumentWidget())) {
        return textTab->isRightPanelCollapsed();
    }
    return false;
}

QString MainWindow::currentDocumentRightPanelLabel() const
{
    if (auto *mapTab = qobject_cast<TherionStudio::MapEditorTab *>(currentDocumentWidget())) {
        return mapTab->rightPanelLabel();
    }
    if (auto *textTab = qobject_cast<TherionStudio::TextEditorTab *>(currentDocumentWidget())) {
        return textTab->rightPanelLabel();
    }
    return tr("Right Panel");
}

void MainWindow::setCurrentDocumentRightPanelCollapsed(bool collapsed)
{
    if (auto *mapTab = qobject_cast<TherionStudio::MapEditorTab *>(currentDocumentWidget())) {
        mapTab->setRightPanelCollapsed(collapsed);
        refreshViewMenuActions();
        return;
    }
    if (auto *textTab = qobject_cast<TherionStudio::TextEditorTab *>(currentDocumentWidget())) {
        textTab->setRightPanelCollapsed(collapsed);
        refreshViewMenuActions();
    }
}

TherionStudio::MapEditorTab *MainWindow::currentDetachedMapTabWithContextHelp() const
{
    auto *mapTab = qobject_cast<TherionStudio::MapEditorTab *>(currentDocumentWidget());
    if (mapTab == nullptr || !mapTab->isMapPaneDetached() || !mapTab->hasContextHelpPanel()) {
        return nullptr;
    }
    return mapTab;
}

bool MainWindow::currentDetachedMapContextHelpCollapsed() const
{
    if (auto *mapTab = currentDetachedMapTabWithContextHelp()) {
        return mapTab->isContextHelpPanelCollapsed();
    }
    return false;
}

void MainWindow::setCurrentDetachedMapContextHelpCollapsed(bool collapsed)
{
    if (auto *mapTab = currentDetachedMapTabWithContextHelp()) {
        mapTab->setContextHelpPanelCollapsed(collapsed);
        refreshViewMenuActions();
    }
}
