#pragma once
#include "services/DeviceRegistry.h"
#include <QDialog>
namespace studio::ui {
QDialog* showDeviceManager(DeviceRegistry*, QWidget* parent);
}
