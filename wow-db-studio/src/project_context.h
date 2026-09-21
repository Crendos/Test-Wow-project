#pragma once
#include <QString>

struct ProjectContextResult { QString content; int files = 0; bool truncated = false; };

// Reads a user-selected source folder locally; binary and oversized files are excluded.
class ProjectContext final {
public:
    static ProjectContextResult loadFolder(const QString &folder, int maxFiles = 60, qsizetype maxChars = 120000);
};
