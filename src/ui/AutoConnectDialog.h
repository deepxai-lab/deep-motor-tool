#pragma once

#include "services/DeviceRegistry.h"
#include <QDialog>
#include <functional>

namespace studio::ui {
QDialog* showAutoConnectDialog(DeviceRegistry* registry, std::function<void()> beforeScan,
                               std::function<void(const DeviceSession&)> connectDiscovered,
                               QWidget* parent);
}
