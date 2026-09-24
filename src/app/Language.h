#pragma once

#include <QString>
#include <QStringList>

namespace kolorit::language {

/// Languages the interface is available in. English is the source language of the
/// code; every other entry has a compiled Qt translation in the resources (:/i18n).
QStringList available();

/// Name of a language in that language itself: "English", "Русский"
QString nativeName(const QString& code);

/// Language chosen in the settings; an empty string means "follow the system"
QString preferred();
void setPreferred(const QString& code);

/// Language actually in use: the preferred one, otherwise the first system UI language
/// that has a translation, otherwise English
QString resolved();

/// Load the translations for the resolved language into the application.
/// Call once, after the QCoreApplication has been created and before any window.
void install();

} // namespace kolorit::language
