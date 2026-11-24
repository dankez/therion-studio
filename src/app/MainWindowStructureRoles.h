#pragma once

#include <Qt>

namespace
{
constexpr int SourceFileRole = Qt::UserRole + 1;
constexpr int LineNumberRole = Qt::UserRole + 2;
constexpr int CategoryRole = Qt::UserRole + 3;
constexpr int NameRole = Qt::UserRole + 4;
constexpr int OverrideKeyRole = Qt::UserRole + 5;
constexpr int ObjectIdRole = Qt::UserRole + 6;
constexpr int ActionRole = Qt::UserRole + 7;
constexpr int DiagnosticKeyRole = Qt::UserRole + 8;
constexpr int ScrapDistortionStateRole = Qt::UserRole + 9;
constexpr int ScrapDistortionOriginalToolTipRole = Qt::UserRole + 10;
constexpr int ScrapDistortionPresentedToolTipRole = Qt::UserRole + 11;
constexpr int ScrapDistortionOutdatedRole = Qt::UserRole + 12;
}
