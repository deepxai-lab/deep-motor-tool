#pragma once
#include <QString>

namespace studio::ui::i18n {
QString languageCode();
bool isEnglish();
void setLanguageCode(const QString& code);
QString text(const QString& source);
}
