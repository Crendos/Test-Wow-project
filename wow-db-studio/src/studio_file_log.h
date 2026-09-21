#pragma once
#include <QString>

// Пишет журналы Studio в <папка exe>/logs/ (QA, Wago, ошибки, копии консоли ядра).
namespace StudioFileLog {
QString dir();
QString append(const QString &category, const QString &text);
}
