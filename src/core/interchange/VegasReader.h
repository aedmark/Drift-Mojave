#pragma once

#include "core/project/Project.h"

#include <optional>
#include <QByteArray>
#include <QMap>
#include <QSet>
#include <QString>

namespace drift::vegas {

// Project summary metadata stored in Sony Wave64 or standard RIFF INFO chunks (INAM, IART, ICMT, etc.)
struct ProjectSummary
{
    QString title;        // INAM (Project Title / Name)
    QString author;       // IART (Artist / Author)
    QString comments;     // ICMT (Comments / Project Notes)
    QString copyright;    // ICOP (Copyright)
    QString engineer;     // IENG (Engineer)
    QString creationDate; // ICRD (Creation Date)
    QString subject;      // ISBJ (Subject)
    QString genre;        // IGNR (Genre)
    QString software;     // ISFT (Software / NLE tool)
    QMap<QString, QString> customFields;

    bool isEmpty() const
    {
        return title.isEmpty() && author.isEmpty() && comments.isEmpty()
            && copyright.isEmpty() && engineer.isEmpty() && creationDate.isEmpty()
            && subject.isEmpty() && genre.isEmpty() && software.isEmpty()
            && customFields.isEmpty();
    }
};

// Parses project summary metadata from Sony Wave64 or standard RIFF INFO chunks.
ProjectSummary parseProjectSummary(const QByteArray &data);

// Returns true if filePath is a Sony/MAGIX VEGAS Pro project (.veg) or contains Wave64 RIFF magic.
bool isVegasProject(const QString &filePath);

// Returns true if data begins with Sony Wave64 RIFF header.
bool isVegasData(const QByteArray &data);

// Reads a Sony/MAGIX VEGAS Pro project (.veg) from disk and converts it to a drift::Project.
// Recursively imports nested .veg timelines as Drift composite sequences.
std::optional<Project> readProject(const QString &filePath, QString *error = nullptr,
                                   QSet<QString> visitedPaths = {}, int depth = 0);

// Reads raw binary VEGAS project data and converts it to a drift::Project.
std::optional<Project> readProjectData(const QByteArray &data, const QString &sourceDir = QString(),
                                       QString *error = nullptr,
                                       QSet<QString> visitedPaths = {}, int depth = 0);

// Scans a .veg file or raw Wave64 payload for proprietary/third-party OFX and VST3 plugins
// that Drift does not natively translate (e.g. Stabilize, colormatch, Red Giant, iZotope RX),
// returning formatted entries such as "Stabilize (3×)".
QStringList collectUnmappedPlugins(const QString &filePath);
QStringList collectUnmappedPluginsFromData(const QByteArray &data);

} // namespace drift::vegas
