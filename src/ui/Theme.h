#pragma once
#include "ThemeColors.h"
#include <QString>

namespace studio::ui {
inline QString styleSheet(const QColor& background = defaultBackground()) {
    const ThemeColors colors(background);
    QString sheet = QString::fromUtf8(R"(
QWidget { color: @text@; font-family: "Microsoft YaHei UI"; font-size: 12px; }
QMainWindow, QDialog { background: @window@; }
QWidget#sidebar { background: @sidebar@; border-right: 1px solid @border@; }
QFrame#card { background: @card@; border: 1px solid @border@; border-radius: 10px; }
QFrame#phaseAngleCard, QFrame#canFdCounterCard { background: @card@; border: 1px solid @border@; border-radius: 10px; }
QFrame#phaseAngleCard QLabel#section, QFrame#canFdCounterCard QLabel#section { color: @text@; font-size: 15px; font-weight: 700; }
QFrame#phaseAngleCard QLabel#muted, QFrame#canFdCounterCard QLabel#muted { color: @muted@; font-size: 11px; font-weight: 500; }
QFrame#phaseAngleCard QLabel[dataValue="true"], QFrame#canFdCounterCard QLabel[dataValue="true"] { color: @accent@; font-family: "Consolas", "Microsoft YaHei UI"; font-size: 18px; font-weight: 700; }
QLabel#phaseAngleStatus, QLabel#canFdCounterStatus { color: @muted@; font-size: 11px; font-weight: 600; }
QFrame#statusPanel { background: @card@; border: 1px solid @border@; border-radius: 7px; }
QFrame#statusWordBanner { background: @card@; border: 1px solid @border@; border-radius: 5px; }
QFrame#statusWordBanner[statusState="normal"] { background: @successSurface@; border-color: @border@; }
QFrame#statusWordBanner[statusState="fault"] { background: @dangerSurface@; border-color: @border@; }
QFrame#statusWordBanner[statusState="waiting"] { background: @card@; border-color: @border@; }
QLabel#globalStatusWordValue { color: @muted@; font-size: 10px; font-weight: 700; }
QFrame#statusWordBanner[statusState="normal"] QLabel#globalStatusWordValue { color: @accent@; }
QFrame#statusWordBanner[statusState="fault"] QLabel#globalStatusWordValue { color: @danger@; }
QLabel#globalStatusWordTime { color: @muted@; font-size: 9px; }
QPushButton#statusWordClear { padding: 2px 7px; min-width: 32px; min-height: 16px; max-height: 20px; font-size: 10px; }
QFrame#scanPanel { background: @card@; border: 1px solid @border@; border-radius: 9px; }
QWidget#plotCard { background: @card@; border: 1px solid @border@; border-radius: 9px; }
QLabel#muted { color: @muted@; }
QLabel#sidebarProtocol { color: @muted@; padding-top: 2px; padding-bottom: 1px; }
QLabel#releaseVersionHeading { color: @accent@; font-size: 18px; font-weight: 700; }
QDialog#releaseNotesDialog { background: @card@; }
QTextBrowser#releaseNotesText { color: @text@; background: @input@; border: 1px solid @border@; border-radius: 8px; padding: 14px; font-size: 13px; }
QLabel#title { font-size: 25px; font-weight: 700; color: @text@; }
QLabel#section { font-size: 15px; font-weight: 600; }
QLabel#brand { font-size: 22px; font-weight: 700; letter-spacing: 2px; color: @accent@; }
QLabel#brandSuffix { font-size: 12px; font-weight: 700; letter-spacing: 1px; color: @text@; padding-top: 6px; }
QLabel#brandSubtitle { font-size: 12px; color: @muted@; letter-spacing: 2px; }
QLabel#metric { font-size: 18px; font-weight: 600; color: @text@; }
QLabel[dataValue="true"] { font-size: 18px; font-weight: 600; color: @text@; }
QLabel[angleReading="true"] { font-size: 34px; font-weight: 700; color: @accent@; }
QLabel#statusMetricName { color: @muted@; font-size: 10px; font-weight: 500; }
QLabel#statusMetric { color: @accent@; font-family: "Consolas", "Microsoft YaHei UI"; font-size: 12px; font-weight: 700; }
QLabel#plotTitle { font-size: 13px; font-weight: 600; color: @text@; }
QLabel#plotHelp { font-size: 10px; color: @muted@; }
QLabel#plotPaused { font-size: 10px; color: @warning@; }
QLabel#statusTime { color: @muted@; font-family: "Consolas", "Microsoft YaHei UI"; font-size: 13px; font-weight: 600; }
QLabel#liveStatus { color: @accent@; font-size: 13px; font-weight: 600; }
QLabel#scanPulse { color: @accent@; font-size: 16px; font-weight: 700; }
QLabel#scanResult { color: @muted@; font-size: 13px; line-height: 1.5; }
QLabel#badge { color: @accent@; background: @successSurface@; border-radius: 5px; padding: 6px 10px; }
QLabel#calibrationStatus { color: @text@; background: @card@; border: 1px solid @border@; border-radius: 7px; padding: 10px 12px; font-size: 14px; font-weight: 700; }
QLabel#calibrationStatus[calibrationState="waiting"] { color: @warning@; background: @warningSurface@; border-color: @border@; }
QLabel#calibrationStatus[calibrationState="running"] { color: @accent@; background: @successSurface@; border-color: @border@; }
QLabel#calibrationStatus[calibrationState="success"] { color: @accent@; background: @successSurface@; border-color: @border@; }
QLabel#calibrationStatus[calibrationState="error"] { color: @danger@; background: @dangerSurface@; border-color: @border@; }
QLabel#calibrationStatus[calibrationState="disconnected"] { color: @muted@; background: @card@; border-color: @border@; }
QFrame#calibrationResultPanel { background: @input@; border: 1px solid @border@; border-radius: 9px; }
QFrame#calibrationOutputResult { background: @card@; border: 1px solid @border@; border-radius: 8px; }
QLabel#calibrationPanelTitle { color: @text@; font-size: 15px; font-weight: 700; }
QLabel#calibrationTarget { color: @accent@; background: @successSurface@; border: 1px solid @border@; border-radius: 6px; padding: 4px 9px; font-size: 12px; font-weight: 700; }
QLabel#calibrationCompletion { color: @warning@; background: @warningSurface@; border: 1px solid @border@; border-radius: 6px; padding: 8px 10px; font-size: 13px; font-weight: 700; }
QLabel#calibrationCompletion[received="true"] { color: @accent@; background: @successSurface@; border-color: @border@; }
QLabel#calibrationOutputTitle { color: @text@; font-size: 14px; font-weight: 700; }
QLabel#calibrationOutputAck { color: @muted@; font-size: 12px; font-weight: 600; }
QLabel#calibrationProtocolKey { color: @muted@; font-size: 12px; font-weight: 600; }
QLabel#calibrationProtocolValue { color: @accent@; font-family: "Consolas", "Microsoft YaHei UI"; font-size: 15px; font-weight: 700; }
QLabel#calibrationCommandResponse { color: @text@; font-family: "Consolas", "Microsoft YaHei UI"; font-size: 13px; font-weight: 700; }
QLabel#calibrationProtocolNote { color: @muted@; font-size: 11px; }
QLabel#calibrationTimeline { color: @text@; font-size: 13px; font-weight: 500; }
QPushButton[calibrationAction="true"] { min-height: 24px; padding: 9px 14px; font-size: 13px; font-weight: 600; }
QPushButton[alwaysAvailableAction="true"] { min-height: 24px; font-size: 13px; font-weight: 700; }
QLabel#connectionIdentity { color: @muted@; background: @card@; border: 1px solid @border@; border-radius: 6px; padding: 7px 12px; font-size: 11px; font-weight: 600; }
QLabel#refreshRate { color: @muted@; background: @card@; border: 1px solid @border@; border-radius: 6px; padding: 7px 10px; font-size: 11px; font-weight: 600; }
QLabel#refreshRate[active="true"] { color: @accent@; background: @successSurface@; border-color: @border@; }
QLabel#connectionIdentity[connectionState="connecting"] { color: @warning@; background: @warningSurface@; border-color: @border@; }
QLabel#connectionIdentity[connectionState="connected"] { color: @accent@; background: @successSurface@; border-color: @border@; }
QLabel#connectionIdentity[connectionState="failed"] { color: @danger@; background: @dangerSurface@; border-color: @border@; }
QLabel#warning { color: @warning@; background: @warningSurface@; border: 1px solid @border@; border-radius: 6px; padding: 8px; }
QPushButton { background: @button@; border: 1px solid @border@; border-radius: 6px; padding: 8px 14px; min-height: 18px; }
QPushButton:hover { background: @hover@; border-color: @accent@; }
QPushButton:pressed { background: @pressed@; }
QPushButton:disabled { color: @disabled@; background: @button@; border-color: @border@; }
QPushButton#primary { background: @accentFill@; color: @onAccent@; border-color: @border@; font-weight: 700; }
QPushButton#primary:hover { background: @accentFill@; }
QPushButton#primary:disabled { background: @button@; color: @disabled@; border-color: @border@; }
QPushButton#danger { background: @dangerSurface@; border-color: @border@; color: @danger@; }
QPushButton#autoConnectEntry { color: @accent@; background: @button@; border: 1px solid @border@; border-radius: 6px; padding: 7px 14px; min-width: 82px; min-height: 20px; max-height: 34px; font-size: 12px; font-weight: 700; }
QPushButton#autoConnectEntry:hover { color: @text@; background: @hover@; border-color: @accent@; }
QPushButton#autoConnectEntry:pressed { background: @pressed@; }
QPushButton#autoConnectEntry:checked { color: @onAccent@; background: @accentFill@; border-color: @border@; }
QPushButton#statusAction { padding: 3px 10px; min-height: 16px; }
QPushButton#sidebarToggle { color: @muted@; background: transparent; border: 1px solid @border@; border-radius: 6px; padding: 0; min-height: 0; font-size: 20px; font-weight: 600; }
QPushButton#sidebarToggle:hover { color: @accent@; background: @hover@; border-color: @accent@; }
QPushButton#nav { text-align: left; border: none; background: transparent; color: @muted@; padding: 8px 14px; }
QPushButton#nav:checked { color: @accent@; background: @successSurface@; border-left: 3px solid @accent@; border-radius: 5px; }
QPushButton#nav:hover { background: @hover@; }
QPushButton#nav[compact="true"] { text-align: center; padding: 8px 3px; }
QPushButton#nav[compact="true"]:checked { border-left: none; border-bottom: 3px solid @accent@; padding-bottom: 5px; }
QLineEdit, QDoubleSpinBox, QSpinBox, QComboBox { color: @text@; background-color: @input@; border: 1px solid @border@; border-radius: 5px; padding: 7px; min-height: 18px; selection-background-color: @selection@; selection-color: @onSelection@; }
QLineEdit:focus, QDoubleSpinBox:focus, QSpinBox:focus, QComboBox:focus { color: @text@; background-color: @input@; border-color: @accent@; }
QLineEdit[validationError="true"] { border-color: @border@; background: @dangerSurface@; }
QLineEdit:disabled { color: @disabled@; }
QComboBox::drop-down { background: @card@; border: none; border-left: 1px solid @border@; width: 25px; }
QComboBox::down-arrow { image: url(:/resources/icons/chevron-down.svg); width: 10px; height: 6px; }
QSpinBox::up-button, QDoubleSpinBox::up-button { background: @card@; border: none; border-left: 1px solid @border@; border-bottom: 1px solid @border@; width: 22px; }
QSpinBox::down-button, QDoubleSpinBox::down-button { background: @card@; border: none; border-left: 1px solid @border@; width: 22px; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(:/resources/icons/chevron-up.svg); width: 10px; height: 6px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(:/resources/icons/chevron-down.svg); width: 10px; height: 6px; }
QComboBoxPrivateContainer { background-color: @card@; border: 1px solid @border@; }
QAbstractItemView { color: @text@; background-color: @card@; alternate-background-color: @card@; border: 1px solid @border@; outline: 0; selection-background-color: @selection@; selection-color: @onSelection@; }
QComboBox QAbstractItemView::item, QListView::item { color: @text@; background-color: @card@; border: none; padding: 6px; }
QComboBox QAbstractItemView::item:hover, QListView::item:hover { color: @text@; background-color: @hover@; }
QComboBox QAbstractItemView::item:selected, QListView::item:selected { color: @onSelection@; background-color: @selection@; }
QMenu { color: @text@; background-color: @card@; border: 1px solid @border@; padding: 4px; }
QMenu::item { padding: 7px 24px; border-radius: 4px; }
QMenu::item:selected { color: @onSelection@; background-color: @selection@; }
QTabWidget::pane { background: @card@; border: 1px solid @border@; border-radius: 6px; top: -1px; }
QTabBar::tab { background: @card@; color: @muted@; border: 1px solid @border@; padding: 8px 18px; margin-right: 2px; }
QTabBar::tab:hover { color: @text@; background: @hover@; }
QTabBar::tab:selected { color: @accent@; background: @card@; border-bottom-color: @card@; }
QWidget#tabPage { background: @card@; }
QProgressBar { background: @input@; border: 1px solid @border@; border-radius: 5px; min-height: 12px; text-align: center; }
QProgressBar::chunk { background: @accentFill@; border-radius: 4px; }
QProgressBar#autoConnectProgress { min-height: 18px; color: @text@; font-weight: 600; }
QCheckBox { color: @text@; spacing: 9px; min-height: 22px; }
QCheckBox::indicator { width: 17px; height: 17px; background: @input@; border: 1px solid @border@; border-radius: 4px; }
QCheckBox::indicator:hover { border-color: @accent@; background: @hover@; }
QCheckBox::indicator:checked { background: @accentFill@; border-color: @border@; image: url(:/resources/icons/check.svg); }
QCheckBox::indicator:disabled { background: @button@; border-color: @border@; }
QCheckBox:disabled { color: @disabled@; }
QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: @sidebar@; width: 8px; }
QScrollBar::handle:vertical { background: @border@; border-radius: 4px; min-height: 25px; }
QSplitter::handle { background: @window@; }
QSplitter::handle:vertical { height: 6px; }
QSplitter::handle:horizontal { width: 6px; }
QPlainTextEdit { background: @input@; border: 1px solid @border@; border-radius: 6px; color: @muted@; }
QPlainTextEdit#eventLogView { background: @input@; border-color: @border@; color: @text@; font-size: 13px; padding: 10px; selection-background-color: @selection@; selection-color: @onSelection@; }
QTableView, QTableWidget { background: @input@; alternate-background-color: @card@; border: 1px solid @border@; border-radius: 6px; gridline-color: @border@; selection-background-color: @selection@; }
QTableView::item, QTableWidget::item { padding: 4px; }
QTableView#responseTable { color: @text@; font-size: 12px; selection-background-color: @selection@; selection-color: @onSelection@; }
QHeaderView::section { background: @card@; color: @muted@; border: none; border-right: 1px solid @border@; border-bottom: 1px solid @border@; padding: 7px; }
QStatusBar { background: @sidebar@; color: @muted@; border-top: 1px solid @border@; padding: 0 8px; }
QStatusBar QWidget#statusMetrics { background: transparent; }
QStatusBar QLabel#statusOperation { color: @muted@; padding: 0 4px 0 18px; }
QToolTip { background: @card@; color: @text@; border: 1px solid @border@; padding: 6px; }

QLabel[statusTone="success"] { color: @success@; }
QLabel[statusTone="warning"] { color: @warning@; }
QLabel[statusTone="danger"] { color: @danger@; }
QLabel[statusTone="muted"] { color: @muted@; }
QLabel#releaseTitle { font-size: 30px; font-weight: 700; color: @text@; }
QFrame#releaseCurrent { background: @activeSurface@; border: 1px solid @border@; border-radius: 14px; }
QLabel#releaseVersion { font-size: 30px; font-weight: 700; color: @accent@; }
QLabel#releaseSubtitle { color: @muted@; font-size: 13px; }
QFrame#releaseCard { background: @card@; border: 1px solid @border@; border-radius: 14px; }
QLabel#releaseCardVersion { color: @text@; font-size: 18px; font-weight: 700; }
QLabel#releaseBadge { background: @activeSurface@; color: @accent@; border-radius: 10px; padding: 4px 10px; font-weight: 600; }
QLabel#releaseGroupTitle { color: @text@; font-size: 16px; font-weight: 700; }
QLabel#releaseItem { color: @muted@; font-size: 13px; }
QLabel#releaseBullet { color: @accent@; font-size: 16px; }
)");
    // 样式按语义读取颜色，不给界面叠加蒙层或限制用户选择的色值。
    sheet.replace("@window@", colors.window.name());
    sheet.replace("@sidebar@", colors.sidebar.name());
    sheet.replace("@card@", colors.card.name());
    sheet.replace("@input@", colors.input.name());
    sheet.replace("@plot@", colors.plot.name());
    sheet.replace("@button@", colors.button.name());
    sheet.replace("@hover@", colors.hover.name());
    sheet.replace("@pressed@", colors.pressed.name());
    sheet.replace("@border@", colors.border.name());
    sheet.replace("@text@", colors.text.name());
    sheet.replace("@muted@", colors.muted.name());
    sheet.replace("@disabled@", colors.disabled.name());
    sheet.replace("@accent@", colors.accent.name());
    sheet.replace("@accentFill@", colors.accentFill.name());
    sheet.replace("@onAccent@", colors.onAccent.name());
    sheet.replace("@selection@", colors.selection.name());
    sheet.replace("@onSelection@", colors.onSelection.name());
    sheet.replace("@success@", colors.success.name());
    sheet.replace("@successSurface@", colors.successSurface.name());
    sheet.replace("@warning@", colors.warning.name());
    sheet.replace("@warningSurface@", colors.warningSurface.name());
    sheet.replace("@danger@", colors.danger.name());
    sheet.replace("@dangerSurface@", colors.dangerSurface.name());
    sheet.replace("@activeSurface@", colors.activeSurface.name());
    sheet.replace("@grid@", colors.grid.name());
    sheet.replace("@actual@", colors.actual.name());
    sheet.replace("@target@", colors.target.name());
    return sheet;
}
} // 命名空间 studio::ui
