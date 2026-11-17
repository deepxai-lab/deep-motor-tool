#pragma once
#include "services/AsyncDeviceService.h"
#include "services/RecordingService.h"
#include <QWidget>
#include <functional>

class QPlainTextEdit;

namespace studio::ui {
struct PageContext {
    AsyncDeviceService* device;
    RecordingService* motionRecorder;
    RecordingService* temperatureRecorder;
    Identity* identity;
    std::function<void()> identityConfirmed;
};
QWidget* createMotionPage(PageContext);
QWidget* createCalibrationPage(PageContext);
QWidget* createLinearityPage(PageContext);
QWidget* createAngleErrorPage(PageContext);
QWidget* createInspectionPage(PageContext);
QWidget* createTemperaturePage(PageContext);
QWidget* createIdentityPage(PageContext);
QWidget* createResponsePage(PageContext);
QWidget* createEventLogPage(QPlainTextEdit* logView, RecordingService* recorder);
void showParameters(AsyncDeviceService*, QWidget* parent);
} // 命名空间 studio::ui
