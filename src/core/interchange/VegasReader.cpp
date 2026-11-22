#include "core/interchange/VegasReader.h"

#include "core/project/clip/Clip.h"
#include "core/project/media/MediaAsset.h"
#include "core/project/Project.h"
#include "core/project/Track.h"
#include "engine/decode/probe/MediaProbe.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>
#include <QtMath>
#include <QUuid>
#include <QXmlStreamReader>
#include <cstring>

namespace drift::vegas {

namespace {

// 16-byte Sony/MAGIX Wave64 RIFF chunk and type GUIDs (little-endian)
static const uint8_t kGuidW64Riff[16] = {
    0x72, 0x69, 0x66, 0x66, 0x2e, 0x91, 0xcf, 0x11, 0xa5, 0xd6, 0x28, 0xdb, 0x04, 0xc1, 0x00, 0x00
};
static const uint8_t kGuidW64List[16] = {
    0x6c, 0x69, 0x73, 0x74, 0x2f, 0x91, 0xcf, 0x11, 0xa5, 0xd6, 0x28, 0xdb, 0x04, 0xc1, 0x00, 0x00
};
static const uint8_t kTypeMediaPool[16] = {
    0x5b, 0x2d, 0x8f, 0xb2, 0x0f, 0x23, 0xd2, 0x11, 0x86, 0xaf, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kTypeTimeline[16] = {
    0x47, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kTypeTrack[16] = {
    0x48, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kTypeEventContainer[16] = {
    0x49, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kTypeEvent[16] = {
    0x4a, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kChunkEventTime[16] = {
    0x6a, 0x20, 0x8d, 0x16, 0x21, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kChunkEventTake[16] = {
    0x6d, 0x20, 0x8d, 0x16, 0x21, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kTypeEventGroupList[16] = {
    0x18, 0x3e, 0xe7, 0xb8, 0x4c, 0x7f, 0x6b, 0x45, 0x93, 0xcd, 0x68, 0x66, 0xb9, 0x5e, 0x5f, 0x2b
};
static const uint8_t kChunkEventGroup[16] = {
    0x32, 0xe1, 0x01, 0xe9, 0x77, 0xc6, 0x04, 0x46, 0xac, 0x9d, 0xdb, 0x0b, 0x87, 0xb4, 0xee, 0xbd
};
static const uint8_t kListEventFx[16] = {
    0xe4, 0xe4, 0xf0, 0x1a, 0x7f, 0x6e, 0xd2, 0x11, 0x87, 0x05, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kChunkEventEffect[16] = {
    0xe6, 0xe4, 0xf0, 0x1a, 0x7f, 0x6e, 0xd2, 0x11, 0x87, 0x05, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kChunkEventPanCrop[16] = {
    0xff, 0xac, 0x4b, 0x88, 0xeb, 0x74, 0x6c, 0x45, 0x88, 0xf3, 0x73, 0x9e, 0x83, 0x90, 0xbd, 0x07
};
static const uint8_t kListAudioTrack[16] = {
    0x26, 0x6c, 0xd4, 0x69, 0x0b, 0x7f, 0xd2, 0x11, 0x87, 0x17, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kChunkAudioTrackProps[16] = {
    0x27, 0x6c, 0xd4, 0x69, 0x0b, 0x7f, 0xd2, 0x11, 0x87, 0x17, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kTypeMarkerList[16] = {
    0xb5, 0x00, 0x09, 0x1f, 0x84, 0xdc, 0x43, 0x47, 0x81, 0xcb, 0x08, 0x77, 0x44, 0x40, 0x45, 0x98
};
static const uint8_t kGuidMarker[16] = {
    0x56, 0x62, 0xf7, 0xab, 0x2d, 0x39, 0xd2, 0x11, 0x86, 0xc7, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kTypeSummaryList[16] = {
    0xbc, 0x94, 0x5f, 0x92, 0x5a, 0x52, 0xd2, 0x11, 0x86, 0xdc, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kGuidSummaryChunk[16] = {
    0x1d, 0x4f, 0x23, 0x71, 0x57, 0x52, 0xd2, 0x11, 0x86, 0xdc, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
// Track envelope list & chunk GUIDs (VEGAS Volume / Pan / Composite / Mute envelopes)
static const uint8_t kListTrackEnvelopes[16] = {
    0x4b, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kChunkTrackEnvelope[16] = {
    0x4c, 0x07, 0x6c, 0x4d, 0x16, 0x23, 0xd2, 0x11, 0x86, 0xb0, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};
static const uint8_t kListTrackAudioFx[16] = {
    0x28, 0x6c, 0xd4, 0x69, 0x0b, 0x7f, 0xd2, 0x11, 0x87, 0x17, 0x00, 0xc0, 0x4f, 0x8e, 0xdb, 0x8a
};

inline quint32 readU32LE(const uint8_t *p)
{
    return static_cast<quint32>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

inline quint64 readU64LE(const uint8_t *p)
{
    const quint64 lo = readU32LE(p);
    const quint64 hi = readU32LE(p + 4);
    return lo | (hi << 32);
}

inline double readDoubleLE(const uint8_t *p)
{
    const quint64 val = readU64LE(p);
    double d = 0.0;
    std::memcpy(&d, &val, sizeof(d));
    return d;
}

inline float readFloatLE(const uint8_t *p)
{
    const quint32 val = readU32LE(p);
    float f = 0.0f;
    std::memcpy(&f, &val, sizeof(f));
    return f;
}

QString readUtf16String(const uint8_t *data, qsizetype size, qsizetype offset)
{
    if (offset + 2 > size)
        return {};
    const auto *chars = reinterpret_cast<const char16_t *>(data + offset);
    const qsizetype maxChars = (size - offset) / 2;
    qsizetype len = 0;
    while (len < maxChars && chars[len] != 0) {
        ++len;
    }
    return QString::fromUtf16(chars, len);
}

struct ParsedMarker
{
    quint32 id = 0;
    TimeUs posUs = 0;
    TimeUs durUs = 0;
    bool isRegion = false;
    QString name;
};

std::optional<ParsedMarker> parseMarkerPayload(const uint8_t *p, qsizetype avail)
{
    if (avail < 28)
        return std::nullopt;

    quint32 ident = 0;
    qint64 posTicks = 0;
    qint64 lenTicks = -1;
    quint32 nameBytes = 0;
    const uint8_t *namePtr = nullptr;

    const quint32 val0 = readU32LE(p);
    const quint32 val4 = readU32LE(p + 4);
    const quint32 val8 = (avail >= 12) ? readU32LE(p + 8) : 0;

    if (val4 == 28 && avail >= 32) {
        // Format A (property format): uint32 dataSize, uint32 hdrSize (28), uint32 id, int64 pos, int64 len, uint32 cbName, wszName...
        ident = readU32LE(p + 8);
        posTicks = static_cast<qint64>(readU64LE(p + 12));
        lenTicks = static_cast<qint64>(readU64LE(p + 20));
        nameBytes = readU32LE(p + 28);
        namePtr = p + 32;
    } else if (val8 == 28 && avail >= 36) {
        // Format B (Wave64 64-bit chunk): uint64 chunkSize, uint32 hdrSize (28), uint32 id, int64 pos, int64 len, uint32 cbName, wszName...
        ident = readU32LE(p + 12);
        posTicks = static_cast<qint64>(readU64LE(p + 16));
        lenTicks = static_cast<qint64>(readU64LE(p + 24));
        nameBytes = readU32LE(p + 32);
        namePtr = p + 36;
    } else if (val0 == 28 && avail >= 28) {
        // Format C (direct payload): uint32 hdrSize (28), uint32 id, int64 pos, int64 len, uint32 cbName, wszName...
        ident = readU32LE(p + 4);
        posTicks = static_cast<qint64>(readU64LE(p + 8));
        lenTicks = static_cast<qint64>(readU64LE(p + 16));
        nameBytes = readU32LE(p + 24);
        namePtr = p + 28;
    } else {
        return std::nullopt;
    }

    if (posTicks < 0)
        posTicks = 0;

    ParsedMarker m;
    m.id = ident;
    m.posUs = static_cast<TimeUs>(posTicks / 10);
    if (lenTicks > 0) {
        m.isRegion = true;
        m.durUs = static_cast<TimeUs>(lenTicks / 10);
    } else {
        m.isRegion = false;
        m.durUs = 0;
    }

    if (nameBytes > 0 && namePtr && (namePtr - p) + static_cast<qsizetype>(nameBytes) <= avail) {
        m.name = readUtf16String(namePtr, nameBytes, 0).trimmed();
    }

    return m;
}

void detectTransitionsForTrack(Track &track)
{
    if (track.clips.size() < 2)
        return;

    QList<int> sortedIndices;
    sortedIndices.reserve(track.clips.size());
    for (int i = 0; i < track.clips.size(); ++i)
        sortedIndices.append(i);

    std::sort(sortedIndices.begin(), sortedIndices.end(), [&](int a, int b) {
        if (track.clips[a].timelineStart != track.clips[b].timelineStart)
            return track.clips[a].timelineStart < track.clips[b].timelineStart;
        return track.clips[a].timelineDuration > track.clips[b].timelineDuration;
    });

    for (int idx = 0; idx < sortedIndices.size() - 1; ++idx) {
        const Clip &fromClip = track.clips[sortedIndices[idx]];
        const Clip &toClip = track.clips[sortedIndices[idx + 1]];

        const TimeUs fromEnd = fromClip.timelineEnd();
        const TimeUs toStart = toClip.timelineStart;

        TimeUs durationUs = 0;
        bool isTransition = false;

        // Case 1: Physical overlap on the timeline
        if (toStart > fromClip.timelineStart && toStart < fromEnd) {
            const TimeUs overlap = fromEnd - toStart;
            if (overlap >= 1000) {
                durationUs = overlap;
                isTransition = true;
            }
        }
        // Case 2: Abutting clips with fade-out on outgoing and fade-in on incoming
        else if (qAbs(toStart - fromEnd) <= 1000 && fromClip.fadeOutUs > 0 && toClip.fadeInUs > 0) {
            const TimeUs span = fromClip.fadeOutUs + toClip.fadeInUs;
            if (span >= 1000) {
                durationUs = span;
                isTransition = true;
            }
        }

        if (isTransition && durationUs > 0) {
            bool exists = false;
            for (const Transition &existing : track.transitions) {
                if (existing.fromClipId == fromClip.id && existing.toClipId == toClip.id) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                Transition tr;
                tr.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                tr.fromClipId = fromClip.id;
                tr.toClipId = toClip.id;
                tr.kindId = QStringLiteral("crossfade");
                tr.durationUs = durationUs;
                tr.easingCurve = FadeCurve::Linear;
                track.transitions.append(tr);
            }
        }
    }
}

qsizetype findUtf16(const uint8_t *data, qsizetype size, const char *ascii, qsizetype startOffset = 0)
{
    const qsizetype asciiLen = static_cast<qsizetype>(std::strlen(ascii));
    if (asciiLen <= 0 || size < asciiLen * 2 || startOffset < 0)
        return -1;
    for (qsizetype i = startOffset; i + asciiLen * 2 <= size; i += 2) {
        bool match = true;
        for (qsizetype j = 0; j < asciiLen; ++j) {
            if (data[i + j * 2] != static_cast<uint8_t>(ascii[j]) || data[i + j * 2 + 1] != 0) {
                match = false;
                break;
            }
        }
        if (match)
            return i;
    }
    return -1;
}

double parseDoubleParam(const uint8_t *data, qsizetype size, const char *paramName, double defaultVal)
{
    const qsizetype pos = findUtf16(data, size, paramName);
    if (pos < 0)
        return defaultVal;
    const qsizetype valPos = pos + static_cast<qsizetype>(std::strlen(paramName) * 2) + 2;
    if (valPos + 8 <= size) {
        const double v = readDoubleLE(data + valPos);
        if (!std::isnan(v) && !std::isinf(v))
            return v;
    }
    return defaultVal;
}

struct VegasParsedRtf
{
    QString text;
    QString fontFamily = QStringLiteral("Inter");
    int pixelSize = 64;
    int fontWeight = 400;
    bool italic = false;
    TextAlign align = TextAlign::Center;
};

VegasParsedRtf parseVegasRtf(const QString &rtf)
{
    VegasParsedRtf res;
    if (rtf.isEmpty())
        return res;

    const int fontTblIdx = rtf.indexOf(QLatin1String("\\fonttbl"));
    if (fontTblIdx >= 0) {
        const int endTbl = rtf.indexOf(QLatin1String("}}"), fontTblIdx);
        if (endTbl > fontTblIdx) {
            const QString tblStr = rtf.mid(fontTblIdx, endTbl - fontTblIdx);
            const int fcIdx = tblStr.indexOf(QLatin1String("fcharset0"));
            if (fcIdx >= 0) {
                const int semi = tblStr.indexOf(QLatin1Char(';'), fcIdx);
                if (semi > fcIdx + 9) {
                    const QString font = tblStr.mid(fcIdx + 9, semi - (fcIdx + 9)).trimmed();
                    if (!font.isEmpty())
                        res.fontFamily = font;
                }
            } else {
                const int f0Idx = tblStr.indexOf(QLatin1String("\\f0"));
                if (f0Idx >= 0) {
                    const int semi = tblStr.indexOf(QLatin1Char(';'), f0Idx);
                    if (semi > f0Idx) {
                        const QString segment = tblStr.mid(f0Idx, semi - f0Idx);
                        const int space = segment.lastIndexOf(QLatin1Char(' '));
                        if (space >= 0) {
                            const QString font = segment.mid(space + 1).trimmed();
                            if (!font.isEmpty())
                                res.fontFamily = font;
                        }
                    }
                }
            }
        }
    }

    if (rtf.contains(QLatin1String("\\ql")))
        res.align = TextAlign::Left;
    else if (rtf.contains(QLatin1String("\\qr")))
        res.align = TextAlign::Right;
    else if (rtf.contains(QLatin1String("\\qc")))
        res.align = TextAlign::Center;

    static const QRegularExpression fsRegex(QStringLiteral(R"(\\fs(\d+))"));
    const auto fsMatch = fsRegex.match(rtf);
    if (fsMatch.hasMatch()) {
        const int halfPoints = fsMatch.captured(1).toInt();
        res.pixelSize = qBound(12, static_cast<int>(halfPoints * 0.8), 256);
    }

    if (rtf.contains(QLatin1String("\\b ")) || rtf.contains(QLatin1String("\\b\\")) || rtf.contains(QLatin1String("\\b1")))
        res.fontWeight = 700;
    if (rtf.contains(QLatin1String("\\i ")) || rtf.contains(QLatin1String("\\i\\")) || rtf.contains(QLatin1String("\\i1")))
        res.italic = true;

    QString text;
    int groupDepth = 0;
    bool skipGroup = false;
    int skipDepth = 0;
    int i = 0;
    const int len = rtf.size();

    while (i < len) {
        const QChar ch = rtf.at(i);
        if (ch == QLatin1Char('{')) {
            ++groupDepth;
            if (rtf.mid(i, 9) == QLatin1String("{\\fonttbl")
                || rtf.mid(i, 9) == QLatin1String("{\\colortb")
                || rtf.mid(i, 9) == QLatin1String("{\\stylesh")
                || rtf.mid(i, 3) == QLatin1String("{\\*")) {
                skipGroup = true;
                skipDepth = groupDepth;
            }
            ++i;
            continue;
        }
        if (ch == QLatin1Char('}')) {
            if (skipGroup && groupDepth == skipDepth)
                skipGroup = false;
            --groupDepth;
            ++i;
            continue;
        }

        if (skipGroup) {
            ++i;
            continue;
        }

        if (ch == QLatin1Char('\\')) {
            const bool isPar = (rtf.mid(i, 4) == QLatin1String("\\par")
                                && (i + 4 >= len || !rtf.at(i + 4).isLetter()));
            const bool isLine = (rtf.mid(i, 5) == QLatin1String("\\line")
                                 && (i + 5 >= len || !rtf.at(i + 5).isLetter()));
            if (isPar || isLine) {
                text.append(QLatin1Char('\n'));
                i += isPar ? 4 : 5;
                while (i < len && (rtf.at(i) == QLatin1Char(' ') || rtf.at(i) == QLatin1Char('\r') || rtf.at(i) == QLatin1Char('\n')))
                    ++i;
                continue;
            }
            if (rtf.mid(i, 2) == QLatin1String("\\'") && i + 4 <= len) {
                bool ok = false;
                const int code = rtf.mid(i + 2, 2).toInt(&ok, 16);
                if (ok) {
                    char c = static_cast<char>(code);
                    text.append(QString::fromLatin1(&c, 1));
                }
                i += 4;
                continue;
            }
            if (rtf.mid(i, 2) == QLatin1String("\\\\")) {
                text.append(QLatin1Char('\\'));
                i += 2;
                continue;
            }
            if (rtf.mid(i, 2) == QLatin1String("\\{")) {
                text.append(QLatin1Char('{'));
                i += 2;
                continue;
            }
            if (rtf.mid(i, 2) == QLatin1String("\\}")) {
                text.append(QLatin1Char('}'));
                i += 2;
                continue;
            }
            ++i;
            while (i < len && (rtf.at(i).isLetterOrNumber() || rtf.at(i) == QLatin1Char('-')))
                ++i;
            if (i < len && rtf.at(i) == QLatin1Char(' '))
                ++i;
            continue;
        }

        if (ch == QLatin1Char('\r') || ch == QLatin1Char('\n')) {
            ++i;
            continue;
        }

        text.append(ch);
        ++i;
    }

    res.text = text.trimmed();
    return res;
}

bool extractTitlesAndText(const uint8_t *payload, qsizetype payloadSize,
                         QString *outText, TextStyle *outStyle,
                         double *outScale, QPointF *outLocation)
{
    const qsizetype rtfPos = findUtf16(payload, payloadSize, "rtf1");
    if (rtfPos < 0)
        return false;

    qsizetype ob = -1;
    for (qsizetype i = rtfPos - 2; i >= 0 && i >= rtfPos - 64; i -= 2) {
        if (payload[i] == '{' && payload[i + 1] == 0) {
            ob = i;
            break;
        }
    }
    if (ob < 0)
        ob = std::max<qsizetype>(0, rtfPos - 4);

    qsizetype cb = ob;
    int depth = 0;
    while (cb + 2 <= payloadSize) {
        if (payload[cb] == '{' && payload[cb + 1] == 0) {
            ++depth;
        } else if (payload[cb] == '}' && payload[cb + 1] == 0) {
            --depth;
            if (depth == 0) {
                cb += 2;
                break;
            }
        }
        cb += 2;
    }

    if (cb <= ob)
        return false;

    const QString rtfStr = QString::fromUtf16(
        reinterpret_cast<const char16_t *>(payload + ob),
        (cb - ob) / 2
    );

    const VegasParsedRtf parsed = parseVegasRtf(rtfStr);
    if (outText)
        *outText = parsed.text;

    if (outStyle) {
        outStyle->fontFamily = parsed.fontFamily;
        outStyle->pixelSize = parsed.pixelSize;
        outStyle->fontWeight = parsed.fontWeight;
        outStyle->italic = parsed.italic;
        outStyle->align = parsed.align;
    }

    QColor textColor = Qt::white;
    const qsizetype tcPos = findUtf16(payload, payloadSize, "TextColor");
    if (tcPos >= 0) {
        const qsizetype valPos = tcPos + static_cast<qsizetype>(std::strlen("TextColor") * 2) + 2;
        if (valPos + 32 <= payloadSize) {
            textColor = QColor::fromRgbF(
                qBound(0.0, readDoubleLE(payload + valPos), 1.0),
                qBound(0.0, readDoubleLE(payload + valPos + 8), 1.0),
                qBound(0.0, readDoubleLE(payload + valPos + 16), 1.0),
                qBound(0.0, readDoubleLE(payload + valPos + 24), 1.0)
            );
        }
    }
    if (outStyle)
        setSolidFill(*outStyle, textColor);

    if (outScale) {
        const double s = parseDoubleParam(payload, payloadSize, "Scale", 1.0);
        if (s > 0.01 && s < 20.0)
            *outScale = s;
    }

    if (outLocation) {
        const qsizetype locPos = findUtf16(payload, payloadSize, "Location");
        if (locPos >= 0) {
            const qsizetype valPos = locPos + static_cast<qsizetype>(std::strlen("Location") * 2) + 2;
            if (valPos + 16 <= payloadSize) {
                *outLocation = QPointF(readDoubleLE(payload + valPos), readDoubleLE(payload + valPos + 8));
            }
        }
    }

    const qsizetype trPos = findUtf16(payload, payloadSize, "Tracking");
    if (trPos >= 0 && outStyle) {
        const double tr = parseDoubleParam(payload, payloadSize, "Tracking", 0.0);
        outStyle->letterSpacing = tr * 20.0;
    }

    const qsizetype owPos = findUtf16(payload, payloadSize, "OutlineWidth");
    if (owPos >= 0 && outStyle) {
        const double ow = parseDoubleParam(payload, payloadSize, "OutlineWidth", 0.0);
        if (ow > 0.001) {
            QColor outlineColor = Qt::black;
            const qsizetype ocPos = findUtf16(payload, payloadSize, "OutlineColor");
            if (ocPos >= 0) {
                const qsizetype ocVal = ocPos + static_cast<qsizetype>(std::strlen("OutlineColor") * 2) + 2;
                if (ocVal + 32 <= payloadSize) {
                    outlineColor = QColor::fromRgbF(
                        qBound(0.0, readDoubleLE(payload + ocVal), 1.0),
                        qBound(0.0, readDoubleLE(payload + ocVal + 8), 1.0),
                        qBound(0.0, readDoubleLE(payload + ocVal + 16), 1.0),
                        qBound(0.0, readDoubleLE(payload + ocVal + 24), 1.0)
                    );
                }
            }
            outStyle->layers.prepend(strokeLayer(ow * 10.0, outlineColor));
        }
    }

    const qsizetype sePos = findUtf16(payload, payloadSize, "ShadowEnable");
    if (sePos >= 0 && outStyle) {
        const qsizetype valPos = sePos + static_cast<qsizetype>(std::strlen("ShadowEnable") * 2) + 2;
        if (valPos + 4 <= payloadSize) {
            const quint32 enable = readU32LE(payload + valPos);
            if (enable != 0) {
                QColor shadowColor = QColor(0, 0, 0, 180);
                const qsizetype scPos = findUtf16(payload, payloadSize, "ShadowColor");
                if (scPos >= 0) {
                    const qsizetype scVal = scPos + static_cast<qsizetype>(std::strlen("ShadowColor") * 2) + 2;
                    if (scVal + 32 <= payloadSize) {
                        shadowColor = QColor::fromRgbF(
                            qBound(0.0, readDoubleLE(payload + scVal), 1.0),
                            qBound(0.0, readDoubleLE(payload + scVal + 8), 1.0),
                            qBound(0.0, readDoubleLE(payload + scVal + 16), 1.0),
                            qBound(0.0, readDoubleLE(payload + scVal + 24), 1.0)
                        );
                    }
                }
                const double ox = parseDoubleParam(payload, payloadSize, "ShadowOffsetX", 0.05) * 50.0;
                const double oy = parseDoubleParam(payload, payloadSize, "ShadowOffsetY", 0.05) * 50.0;
                const double blur = parseDoubleParam(payload, payloadSize, "ShadowBlur", 0.1) * 50.0;
                outStyle->layers.prepend(shadowLayer(shadowColor, ox, oy, blur, shadowColor.alphaF()));
            }
        }
    }

    return true;
}

bool extractSolidColor(const uint8_t *payload, qsizetype payloadSize, QColor *outColor)
{
    const qsizetype cPos = findUtf16(payload, payloadSize, "Color");
    if (cPos >= 0) {
        const qsizetype valPos = cPos + static_cast<qsizetype>(std::strlen("Color") * 2) + 2;
        if (valPos + 32 <= payloadSize) {
            if (outColor) {
                *outColor = QColor::fromRgbF(
                    qBound(0.0, readDoubleLE(payload + valPos), 1.0),
                    qBound(0.0, readDoubleLE(payload + valPos + 8), 1.0),
                    qBound(0.0, readDoubleLE(payload + valPos + 16), 1.0),
                    qBound(0.0, readDoubleLE(payload + valPos + 24), 1.0)
                );
            }
            return true;
        }
    }
    if (outColor)
        *outColor = Qt::black;
    return true;
}

QList<Effect> extractVideoEffects(const uint8_t *payload, qsizetype payloadSize)
{
    QList<Effect> effects;
    if (payloadSize < 16)
        return effects;

    // 1. Levels (Sony / VEGAS levels) -> mapped to Drift's built-in color.grade master curve
    qsizetype lPos = 0;
    while ((lPos = findUtf16(payload, payloadSize, "levels}", lPos)) >= 0) {
        const qsizetype subSize = std::min<qsizetype>(payloadSize - lPos, 800);
        const uint8_t *sub = payload + lPos;

        const double inStart = qBound(0.0, parseDoubleParam(sub, subSize, "InputStart", 0.0), 1.0);
        const double inEnd = qBound(0.0, parseDoubleParam(sub, subSize, "InputEnd", 1.0), 1.0);
        const double gamma = qBound(0.1, parseDoubleParam(sub, subSize, "Gamma", 1.0), 10.0);
        const double outStart = qBound(0.0, parseDoubleParam(sub, subSize, "OutputStart", 0.0), 1.0);
        const double outEnd = qBound(0.0, parseDoubleParam(sub, subSize, "OutputEnd", 1.0), 1.0);

        if (inStart > 0.001 || inEnd < 0.999 || std::abs(gamma - 1.0) > 0.01 || outStart > 0.001 || outEnd < 0.999) {
            QJsonArray pts;
            auto appendPt = [&](double x, double y) {
                x = qBound(0.0, x, 1.0);
                y = qBound(0.0, y, 1.0);
                if (!pts.isEmpty()) {
                    const double prevX = pts.last().toObject().value(QStringLiteral("x")).toDouble();
                    if (x - prevX < 1e-4)
                        return;
                }
                QJsonObject obj;
                obj.insert(QStringLiteral("x"), x);
                obj.insert(QStringLiteral("y"), y);
                obj.insert(QStringLiteral("ease"), QStringLiteral("linear"));
                pts.append(obj);
            };

            const double span = std::max(inEnd - inStart, 1e-5);
            const double invGamma = 1.0 / gamma;
            if (inStart > 1e-4)
                appendPt(0.0, outStart);
            appendPt(inStart, outStart);
            if (std::abs(gamma - 1.0) > 0.01 && span > 0.01) {
                constexpr int kInteriorSamples = 7;
                for (int si = 1; si <= kInteriorSamples; ++si) {
                    const double t = double(si) / double(kInteriorSamples + 1);
                    const double x = inStart + t * span;
                    const double y = outStart + (outEnd - outStart) * std::pow(t, invGamma);
                    appendPt(x, y);
                }
            }
            appendPt(inEnd, outEnd);
            if (inEnd < 1.0 - 1e-4)
                appendPt(1.0, outEnd);

            Effect fx;
            fx.catalogId = QStringLiteral("color.grade");
            fx.name = QStringLiteral("Levels");
            fx.parameters[QStringLiteral("curveMaster")] =
                QString::fromUtf8(QJsonDocument(pts).toJson(QJsonDocument::Compact));
            effects.append(fx);
        }
        lPos += 14;
    }

    // 2. Color Corrector (Sony / VEGAS colorcorrector)
    qsizetype cPos = 0;
    while ((cPos = findUtf16(payload, payloadSize, "colorcorrector}", cPos)) >= 0) {
        const qsizetype subSize = std::min<qsizetype>(payloadSize - cPos, 1000);
        const uint8_t *sub = payload + cPos;

        const double sat = parseDoubleParam(sub, subSize, "Saturation", 1.0);
        const double gamma = parseDoubleParam(sub, subSize, "Gamma", 1.0);
        const double gain = parseDoubleParam(sub, subSize, "Gain", 1.0);
        const double offset = parseDoubleParam(sub, subSize, "Offset", 0.0);

        if (std::abs(sat - 1.0) > 0.01 || std::abs(gamma - 1.0) > 0.01 || std::abs(gain - 1.0) > 0.01 || std::abs(offset) > 0.01) {
            Effect fx;
            fx.catalogId = QStringLiteral("color.grade");
            fx.name = QStringLiteral("Color Corrector");
            fx.parameters[QStringLiteral("saturation")] = sat;
            fx.parameters[QStringLiteral("gammaLuma")] = gamma - 1.0;
            fx.parameters[QStringLiteral("gainLuma")] = gain - 1.0;
            fx.parameters[QStringLiteral("liftLuma")] = offset / 100.0;
            effects.append(fx);
        }
        cPos += 30;
    }

    // 3. Sharpen (Sony / VEGAS sharpen)
    qsizetype sPos = 0;
    while ((sPos = findUtf16(payload, payloadSize, "sharpen}", sPos)) >= 0) {
        const qsizetype subSize = std::min<qsizetype>(payloadSize - sPos, 400);
        const uint8_t *sub = payload + sPos;

        const double amount = parseDoubleParam(sub, subSize, "Amount", 0.0);
        if (amount > 0.01) {
            Effect fx;
            fx.catalogId = QStringLiteral("stylize.sharpen");
            fx.name = QStringLiteral("Sharpen");
            fx.parameters[QStringLiteral("luma_amount")] = amount;
            effects.append(fx);
        }
        sPos += 16;
    }

    // 4. Unsharp Mask (Sony unsharpmask)
    qsizetype uPos = 0;
    while ((uPos = findUtf16(payload, payloadSize, "unsharpmask}", uPos)) >= 0) {
        const qsizetype subSize = std::min<qsizetype>(payloadSize - uPos, 400);
        const uint8_t *sub = payload + uPos;

        const double amount = parseDoubleParam(sub, subSize, "Amount", 0.0);
        if (amount > 0.01) {
            Effect fx;
            fx.catalogId = QStringLiteral("stylize.sharpen");
            fx.name = QStringLiteral("Unsharp Mask");
            fx.parameters[QStringLiteral("luma_amount")] = amount;
            effects.append(fx);
        }
        uPos += 24;
    }

    // 5. Vignette (Sony / VEGAS vignette)
    qsizetype vPos = 0;
    while ((vPos = findUtf16(payload, payloadSize, "vignette}", vPos)) >= 0) {
        const qsizetype subSize = std::min<qsizetype>(payloadSize - vPos, 1000);
        const uint8_t *sub = payload + vPos;

        const double outer = parseDoubleParam(sub, subSize, "Outer", 1.0);
        const double inner = parseDoubleParam(sub, subSize, "Inner", 0.0);
        const double feather = parseDoubleParam(sub, subSize, "Feather", 5.0);
        const double aspect = parseDoubleParam(sub, subSize, "Aspect", 1.0);

        double cx = 0.5;
        double cy = 0.5;
        const qsizetype cp = findUtf16(sub, subSize, "Center");
        if (cp >= 0) {
            const qsizetype cVal = cp + static_cast<qsizetype>(std::strlen("Center") * 2) + 2;
            if (cVal + 16 <= subSize) {
                const double x = readDoubleLE(sub + cVal);
                const double y = readDoubleLE(sub + cVal + 8);
                if (!std::isnan(x) && !std::isnan(y) && !std::isinf(x) && !std::isinf(y)) {
                    cx = x;
                    cy = y;
                }
            }
        }

        QColor color = Qt::black;
        const qsizetype colPos = findUtf16(sub, subSize, "Color");
        if (colPos >= 0) {
            const qsizetype colVal = colPos + static_cast<qsizetype>(std::strlen("Color") * 2) + 2;
            if (colVal + 32 <= subSize) {
                const double r = readDoubleLE(sub + colVal);
                const double g = readDoubleLE(sub + colVal + 8);
                const double b = readDoubleLE(sub + colVal + 16);
                const double a = readDoubleLE(sub + colVal + 24);
                if (!std::isnan(r) && !std::isnan(g) && !std::isnan(b) && !std::isnan(a)) {
                    color = QColor::fromRgbF(
                        qBound(0.0, r, 1.0),
                        qBound(0.0, g, 1.0),
                        qBound(0.0, b, 1.0),
                        qBound(0.0, a, 1.0)
                    );
                }
            }
        }

        Effect fx;
        fx.catalogId = QStringLiteral("stylize.vignette");
        fx.name = QStringLiteral("Vignette");
        const double amount = qBound(0.0, 1.0 - inner / std::max(outer, 0.01), 1.0);
        fx.parameters[QStringLiteral("amount")] = amount;
        fx.parameters[QStringLiteral("feather")] = qBound(0.0, feather / 10.0, 1.0);
        fx.parameters[QStringLiteral("roundness")] = qBound(0.0, (aspect - 0.5) / 1.0, 1.0);
        fx.parameters[QStringLiteral("center.x")] = qBound(0.0, cx, 1.0);
        fx.parameters[QStringLiteral("center.y")] = qBound(0.0, cy, 1.0);
        fx.parameters[QStringLiteral("color")] = color.name();
        effects.append(fx);

        vPos += 18;
    }

    // 6. Color Balance (Sony colorbalance)
    qsizetype cbPos = 0;
    while ((cbPos = findUtf16(payload, payloadSize, "colorbalance}", cbPos)) >= 0) {
        const qsizetype subSize = std::min<qsizetype>(payloadSize - cbPos, 600);
        const uint8_t *sub = payload + cbPos;

        const double red = parseDoubleParam(sub, subSize, "Red", 0.0);
        const double green = parseDoubleParam(sub, subSize, "Green", 0.0);
        const double blue = parseDoubleParam(sub, subSize, "Blue", 0.0);

        if (std::abs(red) > 0.001 || std::abs(green) > 0.001 || std::abs(blue) > 0.001) {
            Effect fx;
            fx.catalogId = QStringLiteral("color.grade");
            fx.name = QStringLiteral("Color Balance");
            fx.parameters[QStringLiteral("temperature")] = qBound(-1.0, red - blue, 1.0);
            fx.parameters[QStringLiteral("tint")] = qBound(-1.0, -green, 1.0);
            effects.append(fx);
        }
        cbPos += 26;
    }

    // 7. Softlight XML
    const QByteArray buf = QByteArray::fromRawData(reinterpret_cast<const char *>(payload), payloadSize);
    const QByteArray startTag("<Softlight");
    const QByteArray endTag("</Softlight>");
    qsizetype xmlSearchPos = 0;
    while (xmlSearchPos + startTag.size() <= payloadSize) {
        const qsizetype startOffset = buf.indexOf(startTag, xmlSearchPos);
        if (startOffset < 0)
            break;
        const qsizetype endOffset = buf.indexOf(endTag, startOffset);
        if (endOffset < 0)
            break;
        const qsizetype xmlEnd = endOffset + endTag.size();
        const QByteArray xmlBytes = buf.mid(startOffset, xmlEnd - startOffset);

        QXmlStreamReader xml(xmlBytes);
        bool enabled = true;
        double strength = 0.0;
        double diffusion = 0.0;
        double stretch = 0.44;

        bool inVignette = false;
        bool vigEnabled = false;
        double vigStrength = 1.0;
        double vigBlur = 0.2;
        double vigX = 0.5;
        double vigY = 0.5;

        while (!xml.atEnd() && !xml.hasError()) {
            xml.readNext();
            if (xml.isStartElement()) {
                const auto name = xml.name();
                if (name == QLatin1String("VignetteEffect")) {
                    inVignette = true;
                } else if (!inVignette) {
                    if (name == QLatin1String("Enabled")) {
                        enabled = (xml.readElementText().trimmed().compare(QLatin1String("true"), Qt::CaseInsensitive) == 0);
                    } else if (name == QLatin1String("Strength")) {
                        strength = xml.readElementText().toDouble();
                    } else if (name == QLatin1String("Diffusion")) {
                        diffusion = xml.readElementText().toDouble();
                    } else if (name == QLatin1String("Stretch")) {
                        stretch = xml.readElementText().toDouble();
                    }
                } else {
                    if (name == QLatin1String("Enabled")) {
                        vigEnabled = (xml.readElementText().trimmed().compare(QLatin1String("true"), Qt::CaseInsensitive) == 0);
                    } else if (name == QLatin1String("Strength")) {
                        vigStrength = xml.readElementText().toDouble();
                    } else if (name == QLatin1String("Blur")) {
                        vigBlur = xml.readElementText().toDouble();
                    } else if (name == QLatin1String("XPosition")) {
                        vigX = xml.readElementText().toDouble();
                    } else if (name == QLatin1String("YPos") || name == QLatin1String("YPosition")) {
                        vigY = xml.readElementText().toDouble();
                    }
                }
            } else if (xml.isEndElement()) {
                if (xml.name() == QLatin1String("VignetteEffect")) {
                    inVignette = false;
                }
            }
        }

        if (enabled) {
            if (diffusion > 0.01 || strength > 0.01) {
                Effect glow;
                glow.catalogId = QStringLiteral("bloom_glow");
                glow.name = QStringLiteral("Softlight Glow");
                glow.parameters[QStringLiteral("intensity")] = qBound(0.0, strength * 1.5, 2.0);
                glow.parameters[QStringLiteral("blurRadius")] = qBound(1.0, stretch * 25.0, 30.0);
                glow.parameters[QStringLiteral("threshold")] = 0.5;
                effects.append(glow);
            }
            if (vigEnabled) {
                Effect vig;
                vig.catalogId = QStringLiteral("stylize.vignette");
                vig.name = QStringLiteral("Vignette");
                vig.parameters[QStringLiteral("amount")] = qBound(0.0, vigStrength, 1.0);
                vig.parameters[QStringLiteral("feather")] = qBound(0.0, vigBlur, 1.0);
                vig.parameters[QStringLiteral("roundness")] = 0.5;
                vig.parameters[QStringLiteral("center.x")] = qBound(0.0, vigX, 1.0);
                vig.parameters[QStringLiteral("center.y")] = qBound(0.0, vigY, 1.0);
                vig.parameters[QStringLiteral("color")] = QStringLiteral("#000000");
                effects.append(vig);
            }
        }

        xmlSearchPos = xmlEnd;
    }

    return effects;
}

QList<Effect> extractAudioEffects(const uint8_t *payload, qsizetype payloadSize)
{
    QList<Effect> effects;
    if (!payload || payloadSize < 16)
        return effects;

    // 1. Track EQ / Paragraphic EQ / Graphic EQ -> utility.eq
    qsizetype eqPos = findUtf16(payload, payloadSize, "Track EQ");
    if (eqPos < 0)
        eqPos = findUtf16(payload, payloadSize, "Paragraphic EQ");
    if (eqPos < 0)
        eqPos = findUtf16(payload, payloadSize, "Graphic EQ");
    if (eqPos >= 0) {
        const uint8_t *sub = payload + eqPos;
        const qsizetype subSize = std::min<qsizetype>(payloadSize - eqPos, 4096);
        Effect fx;
        fx.catalogId = QStringLiteral("utility.eq");
        fx.name = QStringLiteral("EQ");
        const double low = parseDoubleParam(sub, subSize, "LowGain",
                                            parseDoubleParam(sub, subSize, "Low", 0.0));
        const double mid = parseDoubleParam(sub, subSize, "MidGain",
                                            parseDoubleParam(sub, subSize, "Mid", 0.0));
        const double high = parseDoubleParam(sub, subSize, "HighGain",
                                             parseDoubleParam(sub, subSize, "High", 0.0));
        fx.parameters[QStringLiteral("low")] = qBound(-12.0, low, 12.0);
        fx.parameters[QStringLiteral("mid")] = qBound(-12.0, mid, 12.0);
        fx.parameters[QStringLiteral("high")] = qBound(-12.0, high, 12.0);
        effects.append(fx);
    }

    // 2. Track Compressor / ExpressFX Dynamics / Wave Hammer -> utility.compressor
    qsizetype compPos = findUtf16(payload, payloadSize, "Track Compressor");
    if (compPos < 0)
        compPos = findUtf16(payload, payloadSize, "ExpressFX Dynamics");
    if (compPos < 0)
        compPos = findUtf16(payload, payloadSize, "Wave Hammer");
    if (compPos >= 0) {
        const uint8_t *sub = payload + compPos;
        const qsizetype subSize = std::min<qsizetype>(payloadSize - compPos, 4096);
        Effect fx;
        fx.catalogId = QStringLiteral("utility.compressor");
        fx.name = QStringLiteral("Compressor");
        fx.parameters[QStringLiteral("threshold")] =
            qBound(-40.0, parseDoubleParam(sub, subSize, "Threshold", -18.0), -2.0);
        fx.parameters[QStringLiteral("ratio")] =
            qBound(1.5, parseDoubleParam(sub, subSize, "Ratio", 3.0), 12.0);
        fx.parameters[QStringLiteral("attack")] =
            qBound(1.0, parseDoubleParam(sub, subSize, "Attack", 20.0), 200.0);
        fx.parameters[QStringLiteral("release")] =
            qBound(20.0, parseDoubleParam(sub, subSize, "Release", 250.0), 1000.0);
        fx.parameters[QStringLiteral("makeup")] =
            qBound(1.0, parseDoubleParam(sub, subSize, "Makeup", 2.0), 4.0);
        effects.append(fx);
    }

    // 3. Track Noise Gate -> utility.gate
    qsizetype gatePos = findUtf16(payload, payloadSize, "Track Noise Gate");
    if (gatePos < 0)
        gatePos = findUtf16(payload, payloadSize, "Noise Gate");
    if (gatePos >= 0) {
        const uint8_t *sub = payload + gatePos;
        const qsizetype subSize = std::min<qsizetype>(payloadSize - gatePos, 4096);
        Effect fx;
        fx.catalogId = QStringLiteral("utility.gate");
        fx.name = QStringLiteral("Noise Gate");
        fx.parameters[QStringLiteral("threshold")] =
            qBound(0.001, parseDoubleParam(sub, subSize, "Threshold", 0.02), 0.2);
        fx.parameters[QStringLiteral("ratio")] =
            qBound(1.0, parseDoubleParam(sub, subSize, "Ratio", 3.0), 9.0);
        fx.parameters[QStringLiteral("attack")] =
            qBound(1.0, parseDoubleParam(sub, subSize, "Attack", 10.0), 100.0);
        fx.parameters[QStringLiteral("release")] =
            qBound(20.0, parseDoubleParam(sub, subSize, "Release", 250.0), 1000.0);
        effects.append(fx);
    }

    // 4. Reverb / Acoustic Mirror -> space.reverb
    qsizetype revPos = findUtf16(payload, payloadSize, "Reverb");
    if (revPos < 0)
        revPos = findUtf16(payload, payloadSize, "Acoustic Mirror");
    if (revPos >= 0) {
        const uint8_t *sub = payload + revPos;
        const qsizetype subSize = std::min<qsizetype>(payloadSize - revPos, 4096);
        Effect fx;
        fx.catalogId = QStringLiteral("space.reverb");
        fx.name = QStringLiteral("Reverb");
        fx.parameters[QStringLiteral("size")] =
            qBound(0.0, parseDoubleParam(sub, subSize, "RoomSize", 0.5), 1.0);
        fx.parameters[QStringLiteral("damping")] =
            qBound(0.0, parseDoubleParam(sub, subSize, "Damping", 0.5), 1.0);
        fx.parameters[QStringLiteral("width")] =
            qBound(0.0, parseDoubleParam(sub, subSize, "Width", 1.0), 1.0);
        fx.parameters[QStringLiteral("mix")] =
            qBound(0.0, parseDoubleParam(sub, subSize, "WetMix", 0.3), 1.0);
        effects.append(fx);
    }

    // 5. Simple Delay / Multi-Tap Delay -> space.echo
    qsizetype delPos = findUtf16(payload, payloadSize, "Simple Delay");
    if (delPos < 0)
        delPos = findUtf16(payload, payloadSize, "Multi-Tap Delay");
    if (delPos >= 0) {
        const uint8_t *sub = payload + delPos;
        const qsizetype subSize = std::min<qsizetype>(payloadSize - delPos, 4096);
        Effect fx;
        fx.catalogId = QStringLiteral("space.echo");
        fx.name = QStringLiteral("Echo");
        fx.parameters[QStringLiteral("delay")] =
            qBound(10.0, parseDoubleParam(sub, subSize, "DelayMs", 60.0), 900.0);
        fx.parameters[QStringLiteral("decay")] =
            qBound(0.1, parseDoubleParam(sub, subSize, "Decay", 0.4), 0.9);
        fx.parameters[QStringLiteral("in_gain")] = 0.8;
        fx.parameters[QStringLiteral("out_gain")] = 0.88;
        effects.append(fx);
    }

    return effects;
}

void parseTrackEnvelopeChunk(const uint8_t *payload, qsizetype size, Track &track)
{
    if (!payload || size < 8)
        return;
    const quint32 envType = readU32LE(payload);
    const quint32 pointCount = readU32LE(payload + 4);
    if (pointCount == 0 || pointCount > 100000)
        return;

    // Each point: int64 timeTicks (8) + double value (8) + uint32 curveType (4) = 20 bytes
    constexpr qsizetype kPointStride = 20;
    if (8 + static_cast<qsizetype>(pointCount) * kPointStride > size)
        return;

    KeyframeTrack<double> *target = nullptr;
    double minVal = 0.0;
    double maxVal = 2.0;
    switch (envType) {
    case 0: // Volume
        target = &track.volumeEnvelope;
        minVal = 0.0;
        maxVal = 2.0;
        track.showVolumeEnvelope = true;
        break;
    case 1: // Pan
        target = &track.panEnvelope;
        minVal = -1.0;
        maxVal = 1.0;
        track.showPanEnvelope = true;
        break;
    case 2: // Composite level
        target = &track.compositeEnvelope;
        minVal = 0.0;
        maxVal = 1.0;
        track.showCompositeEnvelope = true;
        break;
    case 3: // Mute
        target = &track.muteEnvelope;
        minVal = 0.0;
        maxVal = 1.0;
        track.showMuteEnvelope = true;
        break;
    default:
        return;
    }

    for (quint32 i = 0; i < pointCount; ++i) {
        const uint8_t *pt = payload + 8 + static_cast<qsizetype>(i) * kPointStride;
        const qint64 ticks = static_cast<qint64>(readU64LE(pt));
        const double rawVal = readDoubleLE(pt + 8);
        const quint32 curve = readU32LE(pt + 16);
        if (std::isnan(rawVal) || std::isinf(rawVal))
            continue;
        const TimeUs tUs = qMax<TimeUs>(0, static_cast<TimeUs>(ticks / 10));
        double val = qBound(minVal, rawVal, maxVal);
        if (envType == 3)
            val = rawVal >= 0.5 ? 1.0 : 0.0;
        target->setKeyframe(tUs, val);
        if (curve == 1 || envType == 3)
            target->setEasing(tUs, Interpolation::Hold);
    }
}

QString extractPathFromPayload(const uint8_t *data, qsizetype size)
{
    if (size < 4)
        return {};

    // 1. Search for UTF-16LE Windows drive separator ":\" (0x3a, 0x00, 0x5c, 0x00) or ":/" (0x3a, 0x00, 0x2f, 0x00)
    static const uint8_t kColonBackslash[4] = { 0x3a, 0x00, 0x5c, 0x00 };
    static const uint8_t kColonSlash[4] = { 0x3a, 0x00, 0x2f, 0x00 };
    for (qsizetype i = 0; i + 4 <= size; i += 2) {
        if (std::memcmp(data + i, kColonBackslash, 4) == 0 || std::memcmp(data + i, kColonSlash, 4) == 0) {
            const qsizetype start = (i >= 2) ? (i - 2) : i;
            const QString path = readUtf16String(data, size, start);
            if (!path.isEmpty())
                return path;
        }
    }

    // 2. Search for UTF-16LE "META:\"
    static const uint8_t kMetaPrefix[12] = { 'M', 0, 'E', 0, 'T', 0, 'A', 0, ':', 0, '\\', 0 };
    for (qsizetype i = 0; i + 12 <= size; i += 2) {
        if (std::memcmp(data + i, kMetaPrefix, 12) == 0) {
            const QString path = readUtf16String(data, size, i);
            if (!path.isEmpty())
                return path;
        }
    }

    // 3. Fallback to common VEGAS payload offsets (96, 102, 128, 134, 64)
    for (const qsizetype off : { 96, 102, 128, 134, 64 }) {
        if (off + 2 <= size) {
            const QString s = readUtf16String(data, size, off);
            if (!s.isEmpty() && (s.contains(QLatin1Char('/')) || s.contains(QLatin1Char('\\'))))
                return s;
        }
    }

    return {};
}

QString cleanMediaFilename(const QString &rawPath)
{
    QString s = rawPath;
    s.replace(QLatin1Char('\\'), QLatin1Char('/'));

    // Handle META:\SubClip\{<path>}
    const int openBrace = s.indexOf(QLatin1Char('{'));
    const int closeBrace = s.indexOf(QLatin1Char('}'), openBrace + 1);
    if (openBrace >= 0 && closeBrace > openBrace) {
        const QString inside = s.mid(openBrace + 1, closeBrace - openBrace - 1);
        if (inside.contains(QLatin1Char('/')) || inside.contains(QLatin1Char('.')))
            s = inside;
    }

    QString diskName = QFileInfo(s).fileName();
    const int braceIdx = diskName.indexOf(QLatin1Char('}'));
    if (braceIdx > 0 && !diskName.startsWith(QLatin1Char('{')))
        diskName = diskName.left(braceIdx);
    return diskName;
}

QString resolveMediaFile(const QString &winPath, const QString &diskName,
                         const QString &sourceDir, const QList<QDir> &searchDirs,
                         const QHash<QString, QStringList> &indexedFiles)
{
    if (diskName.isEmpty())
        return {};

    const QString normWin = QString(winPath).replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QFileInfo diskInfo(diskName);
    const QString baseName = diskInfo.completeBaseName();
    const QString ext = diskInfo.suffix().toLower();
    const QString movName = baseName + QStringLiteral(".mov");
    const QString remapFrom = qEnvironmentVariable("DRIFT_MEDIA_REMAP_FROM");
    const QString remapTo = qEnvironmentVariable("DRIFT_MEDIA_REMAP_TO");

    const bool isMov = (ext == QLatin1String("mov"));
    // Audio, images, or documents should not be redirected to .mov
    const bool isVideoCandidate = !isMov && (ext == QLatin1String("mts") || ext == QLatin1String("m2ts")
                                            || ext == QLatin1String("m2t") || ext == QLatin1String("ts")
                                            || ext == QLatin1String("mp4") || ext == QLatin1String("m4v")
                                            || ext == QLatin1String("avi") || ext == QLatin1String("mkv")
                                            || ext == QLatin1String("wmv") || ext == QLatin1String("flv")
                                            || ext == QLatin1String("webm") || ext == QLatin1String("vob")
                                            || ext.isEmpty());

    // Subpath relative matches from winPath
    // E.g. "G:/Against the Odds/Episode 4/Day 4/00017.MTS" -> checks "Day 4/00017.mov" vs "Day 4/00017.MTS"
    QStringList movSubpathCandidates;
    QStringList origSubpathCandidates;
    const QStringList winParts = normWin.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (int i = 1; i <= std::min(qsizetype(4), winParts.size()); ++i) {
        const QString sub = winParts.mid(winParts.size() - i).join(QLatin1Char('/'));
        origSubpathCandidates.append(sub);
        const QFileInfo subInfo(sub);
        const QString subDir = subInfo.path();
        const QString movSub = (subDir.isEmpty() || subDir == QLatin1String("."))
                               ? (subInfo.completeBaseName() + QStringLiteral(".mov"))
                               : (subDir + QLatin1Char('/') + subInfo.completeBaseName() + QStringLiteral(".mov"));
        movSubpathCandidates.append(movSub);
    }

    // 1. If video media, check for converted ProRes .mov FIRST (in same dir, Transcodes_ProRes, or matching subpaths)
    if (isVideoCandidate) {
        for (const QDir &dir : searchDirs) {
            if (!dir.exists())
                continue;
            for (const QString &movSub : movSubpathCandidates) {
                if (dir.exists(movSub))
                    return dir.filePath(movSub);
            }
            // Bare movName is valid if in sourceDir or in a Transcodes_ProRes directory
            if (dir == QDir(sourceDir) || dir.dirName() == QLatin1String("Transcodes_ProRes")) {
                if (dir.exists(movName))
                    return dir.filePath(movName);
            }
        }

        // Fast lookup from project tree index for .mov only when matching parent subdirectory
        const QString movLow = movName.toLower();
        if (indexedFiles.contains(movLow)) {
            const QStringList &candidates = indexedFiles.value(movLow);
            for (const QString &c : candidates) {
                if (winParts.size() >= 2 && c.contains(winParts.at(winParts.size() - 2), Qt::CaseInsensitive))
                    return c;
            }
        }

        // Environment-configured remap fallback for .mov
        if (!remapFrom.isEmpty() && !remapTo.isEmpty() && !sourceDir.isEmpty()) {
            QString altSource = sourceDir;
            if (altSource.startsWith(remapFrom))
                altSource.replace(0, remapFrom.size(), remapTo);

            if (altSource != sourceDir && QDir(altSource).exists()) {
                const QDir altDir(altSource);
                for (const QString &movSub : movSubpathCandidates) {
                    if (altDir.exists(movSub))
                        return altDir.filePath(movSub);
                }
                if (altDir == QDir(altSource) || altDir.dirName() == QLatin1String("Transcodes_ProRes")) {
                    if (altDir.exists(movName))
                        return altDir.filePath(movName);
                }
            }
        }
    }

    // 2. Direct absolute path match for original file
    const QFileInfo direct(diskName);
    if (direct.isAbsolute() && direct.exists())
        return direct.canonicalFilePath();

    const QFileInfo winInfo(normWin);
    if (winInfo.isAbsolute() && winInfo.exists())
        return winInfo.canonicalFilePath();

    // 3. Search directories for original file
    for (const QDir &dir : searchDirs) {
        if (!dir.exists())
            continue;
        for (const QString &sub : origSubpathCandidates) {
            if (dir.exists(sub))
                return dir.filePath(sub);
        }
        if (dir.exists(diskName))
            return dir.filePath(diskName);
    }

    // 4. Fast lookup from project tree index for original filename
    const QString lowName = diskName.toLower();
    if (indexedFiles.contains(lowName)) {
        const QStringList &candidates = indexedFiles.value(lowName);
        for (const QString &c : candidates) {
            if (winParts.size() >= 2 && c.contains(winParts.at(winParts.size() - 2), Qt::CaseInsensitive))
                return c;
        }
        return candidates.first();
    }

    // 5. Environment-configured remap fallback for original file
    if (!remapFrom.isEmpty() && !remapTo.isEmpty() && !sourceDir.isEmpty()) {
        QString altSource = sourceDir;
        if (altSource.startsWith(remapFrom))
            altSource.replace(0, remapFrom.size(), remapTo);

        if (altSource != sourceDir && QDir(altSource).exists()) {
            const QDir altDir(altSource);
            for (const QString &sub : origSubpathCandidates) {
                if (altDir.exists(sub))
                    return altDir.filePath(sub);
            }
            if (altDir.exists(diskName))
                return altDir.filePath(diskName);
        }
    }

    return diskName;
}

struct PanCropKeyframe
{
    qint64 timeTicks = 0;
    double w = 0.0;
    double h = 0.0;
    double cx = 0.0;
    double cy = 0.0;
    double rot = 0.0;
};

struct PanCropData
{
    quint32 sourceWidth = 0;
    quint32 sourceHeight = 0;
    QList<PanCropKeyframe> keyframes;
    std::optional<Mask> mask;
};

PanCropData parsePanCropChunk(const uint8_t *payload, qsizetype size)
{
    PanCropData result;
    if (!payload || size < 32)
        return result;

    // 1. Locate EPCS chunk for source dimensions
    static const uint8_t kFourCCEpcs[4] = {'E', 'P', 'C', 'S'};
    for (qsizetype i = 0; i + 8 <= size; ++i) {
        if (std::memcmp(payload + i, kFourCCEpcs, 4) == 0) {
            const quint32 epcsLen = readU32LE(payload + i + 4);
            const qsizetype dataStart = i + 8;
            if (dataStart + 52 <= size && dataStart + static_cast<qsizetype>(epcsLen) <= size) {
                result.sourceWidth = readU32LE(payload + dataStart + 44);
                result.sourceHeight = readU32LE(payload + dataStart + 48);
            }
            break;
        }
    }

    // 2. Locate POSK chunks for keyframes
    static const uint8_t kFourCCPosk[4] = {'P', 'O', 'S', 'K'};
    qsizetype pos = 0;
    while (pos + 8 <= size) {
        if (std::memcmp(payload + pos, kFourCCPosk, 4) == 0) {
            const quint32 poskLen = readU32LE(payload + pos + 4);
            const qsizetype dataStart = pos + 8;
            if (poskLen >= 60 && dataStart + static_cast<qsizetype>(poskLen) <= size) {
                PanCropKeyframe kf;
                kf.timeTicks = static_cast<qint64>(readU64LE(payload + dataStart));
                kf.w = readDoubleLE(payload + dataStart + 20);
                kf.h = readDoubleLE(payload + dataStart + 28);
                kf.cx = readDoubleLE(payload + dataStart + 36);
                kf.cy = readDoubleLE(payload + dataStart + 44);
                kf.rot = readDoubleLE(payload + dataStart + 52);
                result.keyframes.append(kf);
                pos = dataStart + poskLen;
                continue;
            }
        }
        ++pos;
    }

    // 3. Locate MASK / MSKK Bezier mask path subchunks (flags + feather + vertexCount + (x, y) doubles)
    static const uint8_t kFourCCMask[4] = {'M', 'A', 'S', 'K'};
    static const uint8_t kFourCCMskk[4] = {'M', 'S', 'K', 'K'};
    const double sw = result.sourceWidth > 0 ? double(result.sourceWidth) : 1920.0;
    const double sh = result.sourceHeight > 0 ? double(result.sourceHeight) : 1080.0;
    for (qsizetype i = 0; i + 8 <= size; ++i) {
        if (std::memcmp(payload + i, kFourCCMask, 4) == 0
            || std::memcmp(payload + i, kFourCCMskk, 4) == 0) {
            const quint32 mLen = readU32LE(payload + i + 4);
            const qsizetype dStart = i + 8;
            // Header: uint32 flags (bit0 = negative/invert) + double featherPx + uint32 vertexCount
            if (mLen >= 16 && dStart + static_cast<qsizetype>(mLen) <= size) {
                const quint32 flags = readU32LE(payload + dStart);
                const double feather = readDoubleLE(payload + dStart + 4);
                const quint32 vertexCount = readU32LE(payload + dStart + 12);
                if (vertexCount >= 3 && vertexCount <= 4096
                    && 16 + static_cast<qsizetype>(vertexCount) * 16 <= static_cast<qsizetype>(mLen)) {
                    Mask mask;
                    mask.shape = MaskShape::Freeform;
                    mask.enabled = true;
                    mask.invert = (flags & 0x01) != 0;
                    if (!std::isnan(feather) && !std::isinf(feather) && feather >= 0.0)
                        mask.feather = qBound(0.0, feather, 200.0);
                    mask.points.reserve(static_cast<qsizetype>(vertexCount));
                    for (quint32 v = 0; v < vertexCount; ++v) {
                        const uint8_t *vp = payload + dStart + 16 + static_cast<qsizetype>(v) * 16;
                        const double vx = readDoubleLE(vp);
                        const double vy = readDoubleLE(vp + 8);
                        if (std::isnan(vx) || std::isnan(vy) || std::isinf(vx) || std::isinf(vy))
                            continue;
                        // Accept either normalized [0..1] or source-pixel coordinates.
                        const double nx = (vx > 1.5 || vx < -0.5) ? (vx / sw) : vx;
                        const double ny = (vy > 1.5 || vy < -0.5) ? (vy / sh) : vy;
                        mask.points.append(QPointF(qBound(-0.5, nx, 1.5), qBound(-0.5, ny, 1.5)));
                    }
                    if (mask.points.size() >= 3) {
                        result.mask = mask;
                        break;
                    }
                }
            }
        }
    }

    return result;
}

void applyPanCropToClip(Clip &clip, const PanCropData &panCrop, int canvasWidth, int canvasHeight)
{
    if (panCrop.mask.has_value())
        clip.mask = *panCrop.mask;

    if (panCrop.keyframes.isEmpty())
        return;

    quint32 sw = panCrop.sourceWidth;
    quint32 sh = panCrop.sourceHeight;
    if (sw == 0)
        sw = (canvasWidth > 0) ? canvasWidth : 1920;
    if (sh == 0)
        sh = (canvasHeight > 0) ? canvasHeight : 1080;

    // Check if keyframes are animated (differ in position/zoom/rotation over time)
    bool isAnimated = false;
    if (panCrop.keyframes.size() > 1) {
        const auto &first = panCrop.keyframes.first();
        for (int i = 1; i < panCrop.keyframes.size(); ++i) {
            const auto &kf = panCrop.keyframes[i];
            if (std::abs(kf.w - first.w) > 0.5 || std::abs(kf.h - first.h) > 0.5
                || std::abs(kf.cx - first.cx) > 0.5 || std::abs(kf.cy - first.cy) > 0.5
                || std::abs(kf.rot - first.rot) > 0.001) {
                isAnimated = true;
                break;
            }
        }
    }

    const auto &firstKf = panCrop.keyframes.first();
    const bool isZoomedOut = (firstKf.w > sw + 1.0 || firstKf.h > sh + 1.0);

    if (!isAnimated && !isZoomedOut) {
        // P8-01: Static cropping & letterbox matching into Clip::sourceFrame and Clip::sourceFrameAngle
        const bool isIdentity = std::abs(firstKf.w - sw) < 1.0 && std::abs(firstKf.h - sh) < 1.0
                                && std::abs(firstKf.cx - sw / 2.0) < 1.0
                                && std::abs(firstKf.cy - sh / 2.0) < 1.0
                                && std::abs(firstKf.rot) < 1e-4;
        if (!isIdentity && firstKf.w > 0.0 && firstKf.h > 0.0) {
            const double cropLeft = firstKf.cx - firstKf.w / 2.0;
            const double cropTop = firstKf.cy - firstKf.h / 2.0;
            const double normX = cropLeft / static_cast<double>(sw);
            const double normY = cropTop / static_cast<double>(sh);
            const double normW = firstKf.w / static_cast<double>(sw);
            const double normH = firstKf.h / static_cast<double>(sh);
            clip.sourceFrame = drift::normalizedSourceFrame(normX, normY, normW, normH);
            const double angleDeg = qRadiansToDegrees(firstKf.rot);
            clip.sourceFrameAngle = drift::normalizedSourceAngle(angleDeg);
        }
    } else {
        // P8-02: 2D pan and zoom motion keyframes into Clip::positionX/Y, Clip::scaleX/Y, and Clip::rotation
        clip.sourceFrame = QRectF(0, 0, 1, 1);
        clip.sourceFrameAngle = 0.0;

        const double cWidth = (canvasWidth > 0) ? canvasWidth : sw;
        const double cHeight = (canvasHeight > 0) ? canvasHeight : sh;

        // If the first keyframe is placed after clip start, insert initial keyframe at t = 0
        if (panCrop.keyframes.first().timeTicks > 0) {
            const auto &kf = panCrop.keyframes.first();
            const double sx = static_cast<double>(sw) / kf.w;
            const double sy = static_cast<double>(sh) / kf.h;
            const double px = (cWidth / 2.0) - (kf.cx - sw / 2.0) * sx;
            const double py = (cHeight / 2.0) - (kf.cy - sh / 2.0) * sy;
            const double rotDeg = -qRadiansToDegrees(kf.rot);

            clip.scaleX.setKeyframe(0, sx);
            clip.scaleY.setKeyframe(0, sy);
            clip.positionX.setKeyframe(0, px);
            clip.positionY.setKeyframe(0, py);
            if (std::abs(rotDeg) > 0.001)
                clip.rotation.setKeyframe(0, rotDeg);
        }

        for (const PanCropKeyframe &kf : panCrop.keyframes) {
            if (kf.w <= 0.0 || kf.h <= 0.0)
                continue;
            const TimeUs keyTimeUs = static_cast<TimeUs>(kf.timeTicks / 10);
            const double sx = static_cast<double>(sw) / kf.w;
            const double sy = static_cast<double>(sh) / kf.h;
            const double px = (cWidth / 2.0) - (kf.cx - sw / 2.0) * sx;
            const double py = (cHeight / 2.0) - (kf.cy - sh / 2.0) * sy;
            const double rotDeg = -qRadiansToDegrees(kf.rot);

            clip.scaleX.setKeyframe(keyTimeUs, sx);
            clip.scaleY.setKeyframe(keyTimeUs, sy);
            clip.positionX.setKeyframe(keyTimeUs, px);
            clip.positionY.setKeyframe(keyTimeUs, py);
            if (std::abs(rotDeg) > 0.001 || !clip.rotation.isEmpty()) {
                clip.rotation.setKeyframe(keyTimeUs, rotDeg);
            }
        }
    }
}

} // namespace

bool isVegasData(const QByteArray &data)
{
    if (data.size() < 40)
        return false;
    return std::memcmp(data.constData(), kGuidW64Riff, 16) == 0;
}

bool isVegasProject(const QString &filePath)
{
    if (filePath.endsWith(QLatin1String(".veg"), Qt::CaseInsensitive))
        return true;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray header = file.read(40);
    return isVegasData(header);
}

std::optional<Project> readProject(const QString &filePath, QString *error,
                                   QSet<QString> visitedPaths, int depth)
{
    const QString canon = QFileInfo(filePath).canonicalFilePath();
    if (!canon.isEmpty())
        visitedPaths.insert(canon);

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Could not open file: ") + filePath;
        return std::nullopt;
    }
    const QByteArray data = file.readAll();
    const QString sourceDir = QFileInfo(filePath).canonicalPath();
    return readProjectData(data, sourceDir, error, visitedPaths, depth);
}

ProjectSummary parseProjectSummary(const QByteArray &data)
{
    ProjectSummary summary;
    if (data.size() < 12)
        return summary;

    const auto *bytes = reinterpret_cast<const uint8_t *>(data.constData());
    const qsizetype totalSize = data.size();

    auto assignTag = [&](const QString &tag, const QString &val) {
        if (val.isEmpty())
            return;
        if (tag == QLatin1String("INAM")) {
            if (summary.title.isEmpty()) summary.title = val;
        } else if (tag == QLatin1String("IART")) {
            if (summary.author.isEmpty()) summary.author = val;
        } else if (tag == QLatin1String("ICMT")) {
            if (summary.comments.isEmpty()) summary.comments = val;
        } else if (tag == QLatin1String("ICOP")) {
            if (summary.copyright.isEmpty()) summary.copyright = val;
        } else if (tag == QLatin1String("IENG")) {
            if (summary.engineer.isEmpty()) summary.engineer = val;
        } else if (tag == QLatin1String("ICRD")) {
            if (summary.creationDate.isEmpty()) summary.creationDate = val;
        } else if (tag == QLatin1String("ISBJ")) {
            if (summary.subject.isEmpty()) summary.subject = val;
        } else if (tag == QLatin1String("IGNR")) {
            if (summary.genre.isEmpty()) summary.genre = val;
        } else if (tag == QLatin1String("ISFT")) {
            if (summary.software.isEmpty()) summary.software = val;
        } else {
            summary.customFields.insert(tag, val);
        }
    };

    auto decodeString = [](const uint8_t *payload, qsizetype len) -> QString {
        if (len <= 0)
            return {};
        // Check if string is UTF-16LE (e.g. second byte is 0 for western text, or starts with null)
        bool isUtf16 = false;
        if (len >= 2 && (len % 2 == 0)) {
            if (payload[0] == 0 && payload[1] == 0)
                isUtf16 = true;
            else if (payload[0] != 0 && payload[1] == 0)
                isUtf16 = true;
        }
        if (isUtf16) {
            const auto *chars = reinterpret_cast<const char16_t *>(payload);
            qsizetype charCount = len / 2;
            while (charCount > 0 && chars[charCount - 1] == 0)
                --charCount;
            if (charCount > 0) {
                QString s = QString::fromUtf16(chars, charCount).trimmed();
                if (!s.isEmpty())
                    return s;
            }
        }
        // Fallback to UTF-8 / ASCII
        qsizetype byteCount = len;
        while (byteCount > 0 && payload[byteCount - 1] == 0)
            --byteCount;
        if (byteCount > 0)
            return QString::fromUtf8(reinterpret_cast<const char *>(payload), byteCount).trimmed();
        return {};
    };

    // 1. Sony Wave64 RIFF format
    if (isVegasData(data)) {
        // Find kGuidSummaryChunk or kTypeSummaryList
        qsizetype scanPos = 40;
        while (scanPos + 24 <= totalSize) {
            const void *found = std::memchr(bytes + scanPos, kGuidSummaryChunk[0], totalSize - scanPos);
            if (!found)
                break;
            const qsizetype idx = static_cast<const uint8_t *>(found) - bytes;
            if (idx + 24 > totalSize)
                break;

            if (std::memcmp(bytes + idx, kGuidSummaryChunk, 16) == 0) {
                const quint64 chunkSize = readU64LE(bytes + idx + 16);
                if (chunkSize >= 24 && idx + static_cast<qsizetype>(chunkSize) <= totalSize) {
                    const uint8_t *payload = bytes + idx + 24;
                    const qsizetype payloadSize = static_cast<qsizetype>(chunkSize - 24);
                    if (payloadSize >= 8) {
                        const quint32 fieldCount = readU32LE(payload + 4);
                        qsizetype fieldPos = 8;
                        for (quint32 f = 0; f < fieldCount && fieldPos + 8 <= payloadSize; ++f) {
                            const QString tag = QString::fromLatin1(reinterpret_cast<const char *>(payload + fieldPos), 4);
                            const quint32 fieldLen = readU32LE(payload + fieldPos + 4);
                            fieldPos += 8;
                            if (fieldPos + static_cast<qsizetype>(fieldLen) > payloadSize)
                                break;
                            const QString val = decodeString(payload + fieldPos, fieldLen);
                            assignTag(tag, val);
                            fieldPos += fieldLen;
                        }
                    }
                }
            }
            scanPos = idx + 1;
        }
    }

    // 2. Standard 4-byte RIFF / AVI / WAV LIST 'INFO' chunks
    {
        qsizetype pos = 0;
        while (pos + 12 <= totalSize) {
            const void *found = std::memchr(bytes + pos, 'L', totalSize - pos);
            if (!found)
                break;
            const qsizetype idx = static_cast<const uint8_t *>(found) - bytes;
            if (idx + 12 > totalSize)
                break;

            if (std::memcmp(bytes + idx, "LIST", 4) == 0 && std::memcmp(bytes + idx + 8, "INFO", 4) == 0) {
                const quint32 listSize = readU32LE(bytes + idx + 4);
                const qsizetype listEnd = qMin(totalSize, idx + 8 + static_cast<qsizetype>(listSize));
                qsizetype subPos = idx + 12;
                while (subPos + 8 <= listEnd) {
                    const QString tag = QString::fromLatin1(reinterpret_cast<const char *>(bytes + subPos), 4);
                    const quint32 subSize = readU32LE(bytes + subPos + 4);
                    subPos += 8;
                    if (subPos + static_cast<qsizetype>(subSize) > listEnd)
                        break;
                    const QString val = decodeString(bytes + subPos, subSize);
                    assignTag(tag, val);
                    subPos += (subSize + 1) & ~1; // 2-byte word aligned
                }
            }
            pos = idx + 1;
        }
    }

    return summary;
}

std::optional<Project> readProjectData(const QByteArray &data, const QString &sourceDir,
                                       QString *error,
                                       QSet<QString> visitedPaths, int depth)
{
    if (data.size() < 64 || !isVegasData(data)) {
        if (error)
            *error = QStringLiteral("Not a valid Sony/MAGIX Wave64 RIFF project");
        return std::nullopt;
    }

    const auto *bytes = reinterpret_cast<const uint8_t *>(data.constData());
    const qsizetype totalSize = data.size();

    // Chunk 0: Project settings (starts at offset 40)
    const quint64 c0Size = readU64LE(bytes + 56);
    if (c0Size < 24 || 64 + c0Size - 24 > static_cast<quint64>(totalSize)) {
        if (error)
            *error = QStringLiteral("Corrupted Wave64 project header");
        return std::nullopt;
    }

    const uint8_t *c0Payload = bytes + 64;
    quint32 sampleRate = 48000;
    if (c0Size >= 40)
        sampleRate = readU32LE(c0Payload + 12);
    if (sampleRate < 8000 || sampleRate > 192000)
        sampleRate = 48000;

    double fps = 23.976023976023978;
    if (c0Size >= 48) {
        const double readFps = readDoubleLE(c0Payload + 16);
        if (readFps >= 1.0 && readFps <= 240.0)
            fps = readFps;
    }

    int canvasWidth = 1920;
    int canvasHeight = 1080;
    if (c0Size >= 472) {
        const quint32 wCand = readU32LE(c0Payload + 440);
        const quint32 hCand = readU32LE(c0Payload + 444);
        if (wCand >= 320 && wCand <= 7680 && hCand >= 240 && hCand <= 4320) {
            canvasWidth = static_cast<int>(wCand);
            canvasHeight = static_cast<int>(hCand);
        }
    }

    Project project;
    project.setResolution(canvasWidth, canvasHeight);
    project.setFps(qRound(fps));
    project.setSampleRate(static_cast<int>(sampleRate));

    // Project summary metadata (RIFF INFO chunk: INAM, IART, ICMT, etc.)
    const ProjectSummary summary = parseProjectSummary(data);
    if (!summary.title.isEmpty())
        project.setName(summary.title);
    if (!summary.author.isEmpty())
        project.setAuthor(summary.author);
    if (!summary.comments.isEmpty()) {
        project.setDescription(summary.comments);
        project.setNotes(summary.comments);
    }
    if (!summary.copyright.isEmpty() || !summary.engineer.isEmpty()) {
        QString extra;
        if (!summary.copyright.isEmpty())
            extra += QStringLiteral("Copyright: %1").arg(summary.copyright);
        if (!summary.engineer.isEmpty()) {
            if (!extra.isEmpty()) extra += QLatin1Char('\n');
            extra += QStringLiteral("Engineer: %1").arg(summary.engineer);
        }
        if (project.description().isEmpty()) {
            project.setDescription(extra);
            project.setNotes(extra);
        } else if (!project.description().contains(summary.copyright)) {
            const QString combined = project.description() + QStringLiteral("\n") + extra;
            project.setDescription(combined);
            project.setNotes(combined);
        }
    }

    // Search directories for media resolution
    QList<QDir> searchDirs;
    if (!sourceDir.isEmpty()) {
        const QDir dir(sourceDir);
        searchDirs.append(dir);
        searchDirs.append(QDir(dir.filePath(QStringLiteral("Transcodes_ProRes"))));
        searchDirs.append(QDir(dir.filePath(QStringLiteral("../Transcodes_ProRes/") + dir.dirName())));
        searchDirs.append(QDir(dir.filePath(QStringLiteral("../Transcodes_ProRes"))));
        searchDirs.append(QDir(dir.filePath(QStringLiteral("../../Transcodes_ProRes/") + dir.dirName())));
        searchDirs.append(QDir(dir.filePath(QStringLiteral("../../Transcodes_ProRes"))));
        searchDirs.append(QDir(dir.filePath(QStringLiteral(".."))));
        searchDirs.append(QDir(dir.filePath(QStringLiteral("../.."))));

        const QString extraPaths = qEnvironmentVariable("DRIFT_MEDIA_SEARCH_PATH");
        if (!extraPaths.isEmpty()) {
            const QStringList paths = extraPaths.split(QDir::listSeparator(), Qt::SkipEmptyParts);
            for (const QString &p : paths) {
                searchDirs.append(QDir(p));
                searchDirs.append(QDir(QDir(p).filePath(QStringLiteral("Transcodes_ProRes"))));
            }
        }
    }

    // Index filenames in sourceDir and nearby project directories for fast resolution
    QHash<QString, QStringList> indexedFiles;
    if (!sourceDir.isEmpty()) {
        auto indexDirectory = [&](const QString &dirPath) {
            if (dirPath.isEmpty() || !QDir(dirPath).exists())
                return;
            QDirIterator it(dirPath, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            int count = 0;
            while (it.hasNext() && count < 10000) {
                it.next();
                ++count;
                indexedFiles[it.fileName().toLower()].append(it.filePath());
            }
        };

        indexDirectory(sourceDir);
        QDir parentDir(sourceDir);
        if (parentDir.cdUp()) {
            indexDirectory(parentDir.absolutePath());
            QDir grandParent(parentDir);
            if (grandParent.cdUp())
                indexDirectory(grandParent.absolutePath());
        }

        // Remap index if environment remap is specified
        const QString remapFrom = qEnvironmentVariable("DRIFT_MEDIA_REMAP_FROM");
        const QString remapTo = qEnvironmentVariable("DRIFT_MEDIA_REMAP_TO");
        if (!remapFrom.isEmpty() && !remapTo.isEmpty()) {
            QString altDir = sourceDir;
            if (altDir.startsWith(remapFrom))
                altDir.replace(0, remapFrom.size(), remapTo);
            if (altDir != sourceDir && QDir(altDir).exists()) {
                indexDirectory(altDir);
                QDir altParent(altDir);
                if (altParent.cdUp())
                    indexDirectory(altParent.absolutePath());
            }
        }
    }

    // 1. Parse Media Pool
    struct MediaEntry
    {
        QString rawFilename;
        QString resolvedPath;
        QString assetId;
        QString sequenceId;
        bool isNestedProject = false;
        bool isImage = false;
        bool isTextGenerator = false;
        bool isSolidColorGenerator = false;
        QString textContent;
        TextStyle textStyle;
        double textScale = 1.0;
        QPointF textLocation{0.5, 0.5};
        QColor solidColor{0, 0, 0, 255};
        QString generatorKind;
        QList<Effect> mediaEffects;
    };
    QMap<quint32, MediaEntry> mediaPool;

    qsizetype pos = 40;
    while (pos + 24 <= totalSize) {
        const uint8_t *ptr = bytes + pos;
        const quint64 chunkSize = readU64LE(ptr + 16);
        if (chunkSize < 24 || pos + static_cast<qsizetype>(chunkSize) > totalSize)
            break;

        if (std::memcmp(ptr, kGuidW64List, 16) == 0 && pos + 40 <= totalSize
            && std::memcmp(ptr + 24, kTypeMediaPool, 16) == 0) {
            qsizetype cpos = pos + 40;
            const qsizetype cend = pos + static_cast<qsizetype>(chunkSize);
            while (cpos + 24 <= cend) {
                const uint8_t *cptr = bytes + cpos;
                const quint64 csize = readU64LE(cptr + 16);
                if (csize < 24 || cpos + static_cast<qsizetype>(csize) > cend)
                    break;

                const uint8_t *payload = cptr + 24;
                const qsizetype payloadSize = static_cast<qsizetype>(csize - 24);
                if (payloadSize >= 64) {
                    const quint32 mid = readU32LE(payload + 60);
                    const QString winPath = extractPathFromPayload(payload, payloadSize);

                    const bool isTitles = winPath.contains(QLatin1String("titlesandtext"), Qt::CaseInsensitive)
                                          || findUtf16(payload, payloadSize, "titlesandtext") >= 0;
                    const bool isSolid = winPath.contains(QLatin1String("solidcolor"), Qt::CaseInsensitive)
                                         || winPath.contains(QLatin1String("02FD3B60-986B-11D4"), Qt::CaseInsensitive)
                                         || findUtf16(payload, payloadSize, "solidcolor") >= 0;
                    const bool isSmpte = winPath.contains(QLatin1String("colorbars"), Qt::CaseInsensitive)
                                         || winPath.contains(QLatin1String("smptebars"), Qt::CaseInsensitive)
                                         || findUtf16(payload, payloadSize, "colorbars") >= 0
                                         || findUtf16(payload, payloadSize, "smptebars") >= 0;
                    const bool isGradient = winPath.contains(QLatin1String("colorgradient"), Qt::CaseInsensitive)
                                            || findUtf16(payload, payloadSize, "colorgradient") >= 0;
                    const bool isCreditRoll = winPath.contains(QLatin1String("creditroll"), Qt::CaseInsensitive)
                                              || findUtf16(payload, payloadSize, "creditroll") >= 0;
                    const bool isTimecodeGen = winPath.contains(QLatin1String("timecode"), Qt::CaseInsensitive)
                                               || findUtf16(payload, payloadSize, "timecode") >= 0;

                    if (isTitles) {
                        MediaEntry entry;
                        entry.isTextGenerator = true;
                        extractTitlesAndText(payload, payloadSize, &entry.textContent, &entry.textStyle,
                                             &entry.textScale, &entry.textLocation);
                        entry.rawFilename = entry.textContent.split(QLatin1Char('\n')).first().trimmed();
                        if (entry.rawFilename.size() > 30)
                            entry.rawFilename = entry.rawFilename.left(30) + QStringLiteral("…");
                        if (entry.rawFilename.isEmpty())
                            entry.rawFilename = QStringLiteral("Title");
                        entry.mediaEffects = extractVideoEffects(payload, payloadSize);
                        mediaPool.insert(mid, entry);
                        cpos += (csize + 7) & ~7;
                        continue;
                    }

                    if (isSolid) {
                        MediaEntry entry;
                        entry.isSolidColorGenerator = true;
                        extractSolidColor(payload, payloadSize, &entry.solidColor);
                        entry.rawFilename = QStringLiteral("Solid Color");
                        entry.mediaEffects = extractVideoEffects(payload, payloadSize);
                        mediaPool.insert(mid, entry);
                        cpos += (csize + 7) & ~7;
                        continue;
                    }

                    if (isSmpte || isGradient) {
                        MediaEntry entry;
                        entry.isSolidColorGenerator = true;
                        entry.generatorKind = isSmpte ? QStringLiteral("smpteBars") : QStringLiteral("colorGradient");
                        entry.solidColor = isSmpte ? QColor(191, 191, 191, 255) : QColor(30, 80, 160, 255);
                        entry.rawFilename = isSmpte ? QStringLiteral("SMPTE Bars") : QStringLiteral("Color Gradient");
                        entry.mediaEffects = extractVideoEffects(payload, payloadSize);
                        mediaPool.insert(mid, entry);
                        cpos += (csize + 7) & ~7;
                        continue;
                    }

                    if (isCreditRoll || isTimecodeGen) {
                        MediaEntry entry;
                        entry.isTextGenerator = true;
                        entry.generatorKind = isCreditRoll ? QStringLiteral("creditRoll") : QStringLiteral("timecode");
                        extractTitlesAndText(payload, payloadSize, &entry.textContent, &entry.textStyle,
                                             &entry.textScale, &entry.textLocation);
                        if (entry.textContent.isEmpty()) {
                            entry.textContent = isCreditRoll ? QStringLiteral("Credits")
                                                             : QStringLiteral("00:00:00:00");
                        }
                        entry.rawFilename = isCreditRoll ? QStringLiteral("Credit Roll")
                                                         : QStringLiteral("Timecode");
                        entry.mediaEffects = extractVideoEffects(payload, payloadSize);
                        mediaPool.insert(mid, entry);
                        cpos += (csize + 7) & ~7;
                        continue;
                    }

                    QString diskName = cleanMediaFilename(winPath);
                    if (diskName.isEmpty() || diskName.startsWith(QLatin1Char('{')))
                        diskName = QStringLiteral("media_%1").arg(mid);

                    const QString resolved = resolveMediaFile(winPath, diskName, sourceDir, searchDirs, indexedFiles);
                    const QString assetId = QUuid::createUuid().toString(QUuid::WithoutBraces);

                    const bool isVeg = resolved.endsWith(QLatin1String(".veg"), Qt::CaseInsensitive);
                    if (isVeg && QFile::exists(resolved) && depth < 8) {
                        const QString canon = QFileInfo(resolved).canonicalFilePath();
                        if (!canon.isEmpty() && !visitedPaths.contains(canon)) {
                            QSet<QString> childVisited = visitedPaths;
                            childVisited.insert(canon);
                            QString nestedError;
                            auto optNested = readProject(resolved, &nestedError, childVisited, depth + 1);
                            if (optNested.has_value() && !optNested->tracks().isEmpty()) {
                                drift::Project nestedProj = std::move(*optNested);
                                const QString seqId = project.addSequence(nestedProj.tracks());

                                for (const auto &nAsset : nestedProj.assets()) {
                                    if (!project.assets().contains(nAsset.id)) {
                                        project.assets().insert(nAsset.id, nAsset);
                                        project.assetOrder().append(nAsset.id);
                                    }
                                }

                                for (const QString &subSeqId : nestedProj.sequenceIds()) {
                                    if (!project.hasSequence(subSeqId)) {
                                        project.addSequence(nestedProj.sequenceTracks(subSeqId), subSeqId);
                                    }
                                }

                                MediaAsset asset;
                                asset.id = assetId;
                                asset.kind = MediaKind::Composite;
                                asset.sequenceId = seqId;
                                asset.name = QFileInfo(resolved).completeBaseName();
                                asset.path = resolved;
                                asset.durationUs = nestedProj.sequenceDurationUs({});
                                asset.width = nestedProj.width() > 0 ? nestedProj.width() : canvasWidth;
                                asset.height = nestedProj.height() > 0 ? nestedProj.height() : canvasHeight;
                                asset.fps = nestedProj.fps() > 0 ? nestedProj.fps() : fps;
                                asset.editFriendly = true;

                                project.assets().insert(asset.id, asset);
                                project.assetOrder().append(asset.id);

                                MediaEntry entry;
                                entry.rawFilename = diskName;
                                entry.resolvedPath = resolved;
                                entry.assetId = assetId;
                                entry.sequenceId = seqId;
                                entry.isNestedProject = true;
                                entry.mediaEffects = extractVideoEffects(payload, payloadSize);
                                mediaPool.insert(mid, entry);

                                cpos += (csize + 7) & ~7;
                                continue;
                            }
                        }
                    }

                    auto isStillImagePath = [](const QString &p) {
                        const QString ext = QFileInfo(p).suffix().toLower();
                        return ext == QLatin1String("png") || ext == QLatin1String("jpg")
                            || ext == QLatin1String("jpeg") || ext == QLatin1String("bmp")
                            || ext == QLatin1String("webp") || ext == QLatin1String("tif")
                            || ext == QLatin1String("tiff") || ext == QLatin1String("tga")
                            || ext == QLatin1String("gif");
                    };

                    MediaEntry entry;
                    entry.rawFilename = diskName;
                    entry.resolvedPath = resolved;
                    entry.assetId = assetId;
                    entry.isImage = isStillImagePath(resolved) || isStillImagePath(diskName);
                    entry.mediaEffects = extractVideoEffects(payload, payloadSize);
                    mediaPool.insert(mid, entry);

                    MediaAsset asset;
                    asset.id = assetId;
                    asset.path = resolved;
                    asset.name = QFileInfo(resolved).fileName();
                    asset.effects = entry.mediaEffects;

                    const QFileInfo resolvedInfo(resolved);
                    if (entry.isImage) {
                        asset.kind = MediaKind::Image;
                        asset.editFriendly = true;
                        QSize imgSize;
                        if (resolvedInfo.exists() && resolvedInfo.isFile()) {
                            imgSize = QImageReader(resolved).size();
                            if (!imgSize.isValid()) {
                                const MediaInfo probed = MediaProbe::probe(resolved);
                                if (probed.ok) {
                                    for (const StreamInfo &st : probed.streams) {
                                        if (st.type == StreamInfo::Type::Video && !st.attachedPicture) {
                                            imgSize = QSize(st.width, st.height);
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        asset.width = imgSize.isValid() ? imgSize.width() : canvasWidth;
                        asset.height = imgSize.isValid() ? imgSize.height() : canvasHeight;
                    } else if (resolvedInfo.exists() && resolvedInfo.isFile()) {
                        const MediaInfo probed = MediaProbe::probe(resolved);
                        if (probed.ok) {
                            bool hasVideo = false;
                            for (const StreamInfo &st : probed.streams) {
                                if (st.type == StreamInfo::Type::Video && !st.attachedPicture) {
                                    hasVideo = true;
                                    break;
                                }
                            }
                            asset.kind = hasVideo ? MediaKind::Video : MediaKind::Audio;
                            asset.durationUs = probed.durationUs;
                            for (const StreamInfo &st : probed.streams) {
                                if (st.type == StreamInfo::Type::Video && !st.attachedPicture) {
                                    asset.width = st.width;
                                    asset.height = st.height;
                                    asset.fps = st.fps;
                                    asset.codecName = st.codecName;
                                    if (MediaProbe::isEditFriendlyCodec(st.codecName)
                                        || resolved.endsWith(QLatin1String(".mov"), Qt::CaseInsensitive)) {
                                        asset.editFriendly = true;
                                    }
                                } else if (st.type == StreamInfo::Type::Audio) {
                                    asset.channels = st.channels;
                                    asset.sampleRate = st.sampleRate;
                                    asset.hasAudio = true;
                                    asset.hasAudioKnown = true;
                                }
                            }
                        }
                    } else {
                        const QString ext = resolvedInfo.suffix().toLower();
                        if (ext == QLatin1String("mov") || ext == QLatin1String("mp4")
                            || ext == QLatin1String("mts") || ext == QLatin1String("m2ts")
                            || ext == QLatin1String("mxf") || ext == QLatin1String("avi")
                            || ext == QLatin1String("mkv") || ext == QLatin1String("webm")
                            || ext == QLatin1String("mpg") || ext == QLatin1String("mpeg")
                            || ext == QLatin1String("wmv") || ext == QLatin1String("ts")
                            || winPath.contains(QLatin1String("titlesandtext"), Qt::CaseInsensitive)) {
                            asset.kind = MediaKind::Video;
                            asset.width = canvasWidth;
                            asset.height = canvasHeight;
                            asset.fps = fps;
                            asset.editFriendly = ext == QLatin1String("mov");
                            if (winPath.contains(QLatin1String("titlesandtext"), Qt::CaseInsensitive))
                                asset.name = QStringLiteral("Titles & Text");
                        } else {
                            asset.kind = MediaKind::Audio;
                        }
                    }

                    // Extract embedded source markers from media payload if present
                    qsizetype mScan = 0;
                    while (mScan + 16 <= payloadSize) {
                        const void *found = std::memchr(payload + mScan, kGuidMarker[0], payloadSize - mScan);
                        if (!found)
                            break;
                        const qsizetype midx = static_cast<const uint8_t *>(found) - payload;
                        if (midx + 16 > payloadSize)
                            break;
                        if (std::memcmp(payload + midx, kGuidMarker, 16) == 0) {
                            auto optMarker = parseMarkerPayload(payload + midx + 16, payloadSize - (midx + 16));
                            if (optMarker.has_value() && !optMarker->name.isEmpty()
                                && !optMarker->name.startsWith(QLatin1String("Tempo: "))) {
                                SourceMarker sm;
                                sm.id = QString::number(optMarker->id);
                                sm.timeUs = optMarker->posUs;
                                sm.name = optMarker->name;
                                asset.markers.append(sm);
                            }
                        }
                        mScan = midx + 1;
                    }

                    project.assets().insert(asset.id, asset);
                    project.assetOrder().append(asset.id);
                }
                cpos += (csize + 7) & ~7;
            }
            break;
        }
        pos += (chunkSize + 7) & ~7;
    }

    // 2. Parse Event Group List (kTypeEventGroupList)
    QList<QList<quint32>> eventGroups;
    pos = 40;
    while (pos + 24 <= totalSize) {
        const uint8_t *ptr = bytes + pos;
        const quint64 chunkSize = readU64LE(ptr + 16);
        if (chunkSize < 24 || pos + static_cast<qsizetype>(chunkSize) > totalSize)
            break;

        if (std::memcmp(ptr, kGuidW64List, 16) == 0 && pos + 40 <= totalSize
            && std::memcmp(ptr + 24, kTypeEventGroupList, 16) == 0) {
            qsizetype gpos = pos + 40;
            const qsizetype gend = pos + static_cast<qsizetype>(chunkSize);
            while (gpos + 24 <= gend) {
                const uint8_t *gptr = bytes + gpos;
                const quint64 gsize = readU64LE(gptr + 16);
                if (gsize < 24 || gpos + static_cast<qsizetype>(gsize) > gend)
                    break;

                if (std::memcmp(gptr, kChunkEventGroup, 16) == 0) {
                    const uint8_t *gpayload = gptr + 24;
                    const qsizetype gpayloadSize = static_cast<qsizetype>(gsize - 24);
                    if (gpayloadSize >= 16) {
                        const quint32 eventCount = readU32LE(gpayload + 8);
                        if (eventCount > 0 && gpayloadSize >= static_cast<qsizetype>(16 + eventCount * 8)) {
                            QList<quint32> groupEvents;
                            groupEvents.reserve(eventCount);
                            for (quint32 i = 0; i < eventCount; ++i) {
                                const quint32 evId = readU32LE(gpayload + 16 + i * 8);
                                groupEvents.append(evId);
                            }
                            eventGroups.append(groupEvents);
                        }
                    }
                }
                gpos += (gsize + 7) & ~7;
            }
            break;
        }
        pos += (chunkSize + 7) & ~7;
    }

    // 3. Parse Timeline Tracks & Events
    project.tracks().clear();
    QHash<quint32, QPair<int, int>> eventIdToClipLoc;
    pos = 40;
    int trackNumber = 1;
    while (pos + 24 <= totalSize) {
        const uint8_t *ptr = bytes + pos;
        const quint64 chunkSize = readU64LE(ptr + 16);
        if (chunkSize < 24 || pos + static_cast<qsizetype>(chunkSize) > totalSize)
            break;

        if (std::memcmp(ptr, kGuidW64List, 16) == 0 && pos + 40 <= totalSize
            && std::memcmp(ptr + 24, kTypeTimeline, 16) == 0) {
            qsizetype tpos = pos + 40;
            const qsizetype tend = pos + static_cast<qsizetype>(chunkSize);
            while (tpos + 40 <= tend) {
                const quint64 tsize = readU64LE(bytes + tpos + 16);
                if (tsize < 40 || tpos + static_cast<qsizetype>(tsize) > tend)
                    break;

                if (std::memcmp(bytes + tpos, kGuidW64List, 16) == 0
                    && std::memcmp(bytes + tpos + 24, kTypeTrack, 16) == 0) {
                    // Track Header
                    TrackType trackType = TrackType::Video;
                    bool trackMuted = false;
                    bool trackSolo = false;
                    QString trackCustomName;

                    if (tpos + 64 <= tend) {
                        const quint64 tcsize = readU64LE(bytes + tpos + 56);
                        if (tcsize >= 24 && tpos + 40 + static_cast<qsizetype>(tcsize) <= tend) {
                            const uint8_t *cpayload = bytes + tpos + 64;
                            const qsizetype cpayloadSize = static_cast<qsizetype>(tcsize - 24);

                            if (cpayloadSize >= 8) {
                                const quint32 flags = readU32LE(cpayload + 4);
                                trackSolo = (flags & 0x01) != 0;
                                trackMuted = (flags & 0x02) != 0;
                            }
                            if (cpayloadSize >= 20) {
                                const quint32 nameBytes = readU32LE(cpayload + 16);
                                if (nameBytes > 0 && cpayloadSize >= 56 + static_cast<qsizetype>(nameBytes)) {
                                    const qsizetype strLen = std::min<qsizetype>(cpayloadSize - 56, nameBytes);
                                    trackCustomName = readUtf16String(cpayload + 56, strLen, 0).trimmed();
                                }
                            }
                            if (cpayloadSize >= 36) {
                                const quint32 trackTypeCode = readU32LE(cpayload + 32);
                                trackType = (trackTypeCode == 2) ? TrackType::Video : TrackType::Audio;
                            }
                        }
                    }

                    Track track;
                    track.type = trackType;
                    track.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    track.number = trackNumber++;
                    track.muted = trackMuted;
                    track.hidden = (trackType == TrackType::Video) ? trackMuted : false;
                    track.solo = trackSolo;
                    if (trackType == TrackType::Video) {
                        const uint8_t *tPtr = bytes + tpos;
                        const qsizetype tScan = static_cast<qsizetype>(std::min<quint64>(tsize, 1024));
                        if (findUtf16(tPtr, tScan, "CompositeMultiply") >= 0)
                            track.compositeBlendMode = BlendMode::Multiply;
                        else if (findUtf16(tPtr, tScan, "CompositeScreen") >= 0)
                            track.compositeBlendMode = BlendMode::Screen;
                        else if (findUtf16(tPtr, tScan, "CompositeOverlay") >= 0)
                            track.compositeBlendMode = BlendMode::Overlay;
                        else if (findUtf16(tPtr, tScan, "CompositeAdd") >= 0)
                            track.compositeBlendMode = BlendMode::Add;
                        else if (findUtf16(tPtr, tScan, "CompositeDifference") >= 0)
                            track.compositeBlendMode = BlendMode::Difference;
                        else if (findUtf16(tPtr, tScan, "CompositeDarken") >= 0)
                            track.compositeBlendMode = BlendMode::Darken;
                        else if (findUtf16(tPtr, tScan, "CompositeLighten") >= 0)
                            track.compositeBlendMode = BlendMode::Lighten;
                    }
                    if (!trackCustomName.isEmpty()) {
                        track.name = trackCustomName;
                    } else {
                        track.name = QStringLiteral("Track %1 (%2)")
                                          .arg(track.number)
                                          .arg(trackType == TrackType::Video ? QStringLiteral("video")
                                                                             : QStringLiteral("audio"));
                    }

                    qsizetype evpos = tpos + 40;
                    const qsizetype evend = tpos + static_cast<qsizetype>(tsize);
                    while (evpos + 24 <= evend) {
                        const quint64 evsize = readU64LE(bytes + evpos + 16);
                        if (evsize < 24 || evpos + static_cast<qsizetype>(evsize) > evend)
                            break;

                        if (evsize >= 40 && std::memcmp(bytes + evpos, kGuidW64List, 16) == 0
                            && std::memcmp(bytes + evpos + 24, kTypeEventContainer, 16) == 0) {
                            qsizetype epos = evpos + 40;
                            const qsizetype eend = evpos + static_cast<qsizetype>(evsize);
                            while (epos + 40 <= eend) {
                                const quint64 esize = readU64LE(bytes + epos + 16);
                                if (esize < 40 || epos + static_cast<qsizetype>(esize) > eend)
                                    break;

                                if (std::memcmp(bytes + epos, kGuidW64List, 16) == 0
                                    && std::memcmp(bytes + epos + 24, kTypeEvent, 16) == 0) {
                                    qsizetype cpos = epos + 40;
                                    const qsizetype cend = epos + static_cast<qsizetype>(esize);
                                    quint64 startTicks = 0;
                                    quint64 durTicks = 0;
                                    quint64 fadeInTicks = 0;
                                    quint64 fadeOutTicks = 0;
                                    quint64 srcInTicks = 0;
                                    quint32 mid = 0;
                                    quint32 eventId = 0;
                                    quint32 eventActiveTakeIndex = 0;
                                    double playbackRate = 1.0;
                                    bool eventMuted = false;
                                    bool eventLocked = false;
                                    VelocityEnvelope eventVelocity;
                                    QList<Effect> takeEffects;
                                    PanCropData panCropData;
                                    struct VegasParsedTake {
                                        quint64 srcInTicks = 0;
                                        quint32 mid = 0;
                                        bool isActive = false;
                                        QList<Effect> effects;
                                    };
                                    QList<VegasParsedTake> parsedTakes;

                                    while (cpos + 24 <= cend) {
                                        const uint8_t *cptr = bytes + cpos;
                                        const quint64 csize = readU64LE(cptr + 16);
                                        if (csize < 24 || cpos + static_cast<qsizetype>(csize) > cend)
                                            break;

                                        const uint8_t *cpayload = cptr + 24;
                                        const qsizetype cpayloadSize = static_cast<qsizetype>(csize - 24);

                                        if (std::memcmp(cptr, kChunkEventTime, 16) == 0) {
                                            if (cpayloadSize >= 32) {
                                                startTicks = readU64LE(cpayload + 16);
                                                durTicks = readU64LE(cpayload + 24);
                                            }
                                            if (cpayloadSize >= 36) {
                                                eventActiveTakeIndex = readU32LE(cpayload + 32);
                                            }
                                            if (cpayloadSize >= 48) {
                                                const double rate = readDoubleLE(cpayload + 40);
                                                if (!std::isnan(rate) && !std::isinf(rate) && rate >= 0.01 && rate <= 100.0)
                                                    playbackRate = rate;
                                            }
                                            if (cpayloadSize >= 68) {
                                                eventId = readU32LE(cpayload + 64);
                                            }
                                            if (cpayloadSize >= 152) {
                                                fadeInTicks = readU64LE(cpayload + 136);
                                                fadeOutTicks = readU64LE(cpayload + 144);
                                            }
                                            if (findUtf16(cpayload, cpayloadSize, "EventMute") >= 0)
                                                eventMuted = true;
                                            if (findUtf16(cpayload, cpayloadSize, "EventLocked") >= 0)
                                                eventLocked = true;
                                            const qsizetype velPos = findUtf16(cpayload, cpayloadSize, "EventVelocity");
                                            if (velPos >= 0) {
                                                const qsizetype vStart = velPos + static_cast<qsizetype>(std::strlen("EventVelocity") * 2) + 2;
                                                if (vStart + 4 <= cpayloadSize) {
                                                    const quint32 count = readU32LE(cpayload + vStart);
                                                    if (count > 0 && count <= 1024
                                                        && vStart + 4 + static_cast<qsizetype>(count) * 16 <= cpayloadSize) {
                                                        QList<VelocityPoint> vPts;
                                                        for (quint32 vi = 0; vi < count; ++vi) {
                                                            const double vp = readDoubleLE(cpayload + vStart + 4 + vi * 16);
                                                            const double vv = readDoubleLE(cpayload + vStart + 4 + vi * 16 + 8);
                                                            if (!std::isnan(vp) && !std::isnan(vv)) {
                                                                VelocityPoint pt;
                                                                pt.pos = qBound(0.0, vp, 1.0);
                                                                pt.velocity = qBound(-1.0, vv, 10.0);
                                                                vPts.append(pt);
                                                            }
                                                        }
                                                        if (!vPts.isEmpty()) {
                                                            eventVelocity.setPoints(vPts);
                                                            eventVelocity.setEnabled(true);
                                                        }
                                                    }
                                                }
                                            }
                                        } else if (std::memcmp(cptr, kChunkEventTake, 16) == 0) {
                                            if (cpayloadSize >= 40) {
                                                srcInTicks = readU64LE(cpayload + 16);
                                                const quint32 takeFlags = readU32LE(cpayload + 32);
                                                mid = readU32LE(cpayload + 36);
                                                parsedTakes.append({srcInTicks, mid, (takeFlags & 0x20u) != 0, {}});
                                            }
                                        } else if ((std::memcmp(cptr, kGuidW64List, 16) == 0 && cpayloadSize >= 16
                                                    && std::memcmp(cpayload, kListEventFx, 16) == 0)
                                                   || std::memcmp(cptr, kListEventFx, 16) == 0) {
                                            qsizetype fpos = (std::memcmp(cptr, kGuidW64List, 16) == 0) ? (cpos + 40) : (cpos + 24);
                                            const qsizetype fend = cpos + static_cast<qsizetype>(csize);
                                            while (fpos + 24 <= fend) {
                                                const uint8_t *fptr = bytes + fpos;
                                                const quint64 fsize = readU64LE(fptr + 16);
                                                if (fsize < 24 || fpos + static_cast<qsizetype>(fsize) > fend)
                                                    break;
                                                const uint8_t *fpayload = fptr + 24;
                                                const qsizetype fpayloadSize = static_cast<qsizetype>(fsize - 24);
                                                const QList<Effect> fx = extractVideoEffects(fpayload, fpayloadSize);
                                                if (!fx.isEmpty()) {
                                                    takeEffects.append(fx);
                                                    if (!parsedTakes.isEmpty())
                                                        parsedTakes.last().effects.append(fx);
                                                }
                                                fpos += (fsize + 7) & ~7;
                                            }
                                        } else if (std::memcmp(cptr, kChunkEventPanCrop, 16) == 0) {
                                            panCropData = parsePanCropChunk(cpayload, cpayloadSize);
                                        }
                                        cpos += (csize + 7) & ~7;
                                    }

                                    int activeTakeIdx = 0;
                                    if (!parsedTakes.isEmpty()) {
                                        bool foundActiveFlag = false;
                                        for (int ti = 0; ti < parsedTakes.size(); ++ti) {
                                            if (parsedTakes.at(ti).isActive) {
                                                activeTakeIdx = ti;
                                                foundActiveFlag = true;
                                                break;
                                            }
                                        }
                                        if (!foundActiveFlag && eventActiveTakeIndex < static_cast<quint32>(parsedTakes.size()))
                                            activeTakeIdx = static_cast<int>(eventActiveTakeIndex);
                                        srcInTicks = parsedTakes.at(activeTakeIdx).srcInTicks;
                                        mid = parsedTakes.at(activeTakeIdx).mid;
                                        if (parsedTakes.size() > 1 && !parsedTakes.at(activeTakeIdx).effects.isEmpty())
                                            takeEffects = parsedTakes.at(activeTakeIdx).effects;
                                    }

                                    if (durTicks > 0) {
                                        Clip clip;
                                        clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);

                                        bool isNested = false;
                                        QString nestedSeqId;
                                        if (mediaPool.contains(mid)) {
                                            const MediaEntry &m = mediaPool[mid];
                                            clip.assetId = m.assetId;
                                            clip.path = m.resolvedPath;
                                            clip.name = QFileInfo(m.resolvedPath).fileName();
                                            if (m.isTextGenerator) {
                                                clip.type = ClipType::Text;
                                                clip.name = m.rawFilename.isEmpty() ? QStringLiteral("Title") : m.rawFilename;
                                                clip.textContent = m.textContent;
                                                clip.textStyle = m.textStyle;
                                                clip.assetId.clear();
                                                clip.path.clear();
                                                if (m.textScale > 0.01 && std::abs(m.textScale - 1.0) > 0.01) {
                                                    clip.scaleX.setKeyframe(0, m.textScale);
                                                    clip.scaleY.setKeyframe(0, m.textScale);
                                                }
                                            } else if (m.isSolidColorGenerator) {
                                                clip.type = ClipType::Shape;
                                                clip.name = m.rawFilename.isEmpty() ? QStringLiteral("Solid Color") : m.rawFilename;
                                                clip.shapeStyle.kind = ShapeKind::Rectangle;
                                                clip.shapeStyle.setSolidFill(m.solidColor);
                                                clip.shapeStyle.setStroke(0.0);
                                                clip.assetId.clear();
                                                clip.path.clear();
                                            } else if (m.isNestedProject && !m.sequenceId.isEmpty()) {
                                                isNested = true;
                                                nestedSeqId = m.sequenceId;
                                                clip.name = QFileInfo(m.resolvedPath).completeBaseName();
                                            }
                                            if (clip.assetId.isEmpty()) {
                                                for (const Effect &fx : m.mediaEffects) {
                                                    clip.effects.append(fx);
                                                }
                                            }
                                        } else {
                                            clip.name = QStringLiteral("clip_%1").arg(track.clips.size());
                                        }

                                        for (const Effect &fx : takeEffects) {
                                            clip.effects.append(fx);
                                        }

                                        if (isNested) {
                                            if (trackType == TrackType::Video) {
                                                clip.type = ClipType::Composite;
                                                clip.sequenceId = nestedSeqId;
                                            } else {
                                                clip.type = ClipType::Audio;
                                                clip.sequenceId = nestedSeqId;
                                            }
                                        } else if (clip.type != ClipType::Text && clip.type != ClipType::Shape) {
                                            if (trackType == TrackType::Video && mediaPool.contains(mid)
                                                && mediaPool[mid].isImage) {
                                                clip.type = ClipType::Image;
                                            } else {
                                                clip.type = (trackType == TrackType::Video) ? ClipType::Video
                                                                                            : ClipType::Audio;
                                            }
                                        }

                                        // Convert 100ns ticks to microseconds
                                        const TimeUs timelineDurUs = static_cast<TimeUs>(durTicks / 10);
                                        const TimeUs srcDurUs = (std::abs(playbackRate - 1.0) > 1e-6)
                                            ? static_cast<TimeUs>(std::llround(static_cast<double>(timelineDurUs) * playbackRate))
                                            : timelineDurUs;
                                        clip.timelineStart = static_cast<TimeUs>(startTicks / 10);
                                        clip.timelineDuration = timelineDurUs;
                                        clip.srcIn = static_cast<TimeUs>(srcInTicks / 10);
                                        clip.srcOut = clip.srcIn + srcDurUs;
                                        clip.speed = playbackRate;
                                        clip.fadeInUs = static_cast<TimeUs>(fadeInTicks / 10);
                                        clip.fadeOutUs = static_cast<TimeUs>(fadeOutTicks / 10);
                                        clip.enabled = !eventMuted;
                                        clip.locked = eventLocked;
                                        if (eventVelocity.enabled() && !eventVelocity.isEmpty())
                                            clip.velocityEnvelope = eventVelocity;

                                        if (trackType == TrackType::Video) {
                                            if ((panCropData.sourceWidth == 0 || panCropData.sourceHeight == 0)
                                                && mediaPool.contains(mid)) {
                                                if (const MediaAsset *a = project.asset(mediaPool[mid].assetId)) {
                                                    panCropData.sourceWidth = a->width > 0 ? a->width : canvasWidth;
                                                    panCropData.sourceHeight = a->height > 0 ? a->height : canvasHeight;
                                                }
                                            }
                                            applyPanCropToClip(clip, panCropData, canvasWidth, canvasHeight);
                                        }

                                        if (parsedTakes.size() > 1) {
                                            for (int ti = 0; ti < parsedTakes.size(); ++ti) {
                                                const VegasParsedTake &pt = parsedTakes.at(ti);
                                                ClipTake take;
                                                take.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                                                if (mediaPool.contains(pt.mid)) {
                                                    const MediaEntry &m = mediaPool[pt.mid];
                                                    take.assetId = m.assetId;
                                                    take.path = m.resolvedPath;
                                                    take.name = m.rawFilename.isEmpty() ? QFileInfo(m.resolvedPath).fileName() : m.rawFilename;
                                                } else {
                                                    take.name = QStringLiteral("Take %1").arg(ti + 1);
                                                }
                                                take.srcIn = static_cast<TimeUs>(pt.srcInTicks / 10);
                                                take.srcOut = take.srcIn + srcDurUs;
                                                take.effects = pt.effects;
                                                clip.takes.append(take);
                                            }
                                            clip.activeTakeIndex = activeTakeIdx;
                                        }

                                        track.clips.append(clip);
                                        if (eventId > 0) {
                                            eventIdToClipLoc.insert(eventId, qMakePair(project.tracks().size(), track.clips.size() - 1));
                                        }
                                    }
                                }
                                epos += (esize + 7) & ~7;
                            }
                        } else if (evsize >= 40 && std::memcmp(bytes + evpos, kGuidW64List, 16) == 0
                                   && std::memcmp(bytes + evpos + 24, kListAudioTrack, 16) == 0) {
                            qsizetype apos = evpos + 40;
                            const qsizetype aend = evpos + static_cast<qsizetype>(evsize);
                            while (apos + 24 <= aend) {
                                const uint8_t *aptr = bytes + apos;
                                const quint64 asize = readU64LE(aptr + 16);
                                if (asize < 24 || apos + static_cast<qsizetype>(asize) > aend)
                                    break;

                                if (std::memcmp(aptr, kChunkAudioTrackProps, 16) == 0) {
                                    const uint8_t *apayload = aptr + 24;
                                    const qsizetype apayloadSize = static_cast<qsizetype>(asize - 24);
                                    if (apayloadSize >= 16) {
                                        const float vol = readFloatLE(apayload + 12);
                                        if (!std::isnan(vol) && !std::isinf(vol) && vol >= 0.0f)
                                            track.volume = static_cast<double>(vol);
                                    }
                                    if (apayloadSize >= 20) {
                                        float pan = readFloatLE(apayload + 16);
                                        if (pan == 0.0f && apayloadSize >= 24)
                                            pan = readFloatLE(apayload + 20);
                                        if (!std::isnan(pan) && !std::isinf(pan))
                                            track.pan = qBound(-1.0, static_cast<double>(pan), 1.0);
                                    }
                                } else if (std::memcmp(aptr, kChunkTrackEnvelope, 16) == 0) {
                                    parseTrackEnvelopeChunk(aptr + 24, static_cast<qsizetype>(asize - 24), track);
                                } else if ((std::memcmp(aptr, kGuidW64List, 16) == 0 && asize >= 40
                                            && (std::memcmp(aptr + 24, kListTrackAudioFx, 16) == 0
                                                || std::memcmp(aptr + 24, kListEventFx, 16) == 0))
                                           || std::memcmp(aptr, kChunkEventEffect, 16) == 0) {
                                    const QList<Effect> afx = extractAudioEffects(aptr + 24, static_cast<qsizetype>(asize - 24));
                                    for (const Effect &e : afx)
                                        track.audioEffects.append(e);
                                }
                                apos += (asize + 7) & ~7;
                            }
                        } else if (std::memcmp(bytes + evpos, kChunkAudioTrackProps, 16) == 0) {
                            const uint8_t *apayload = bytes + evpos + 24;
                            const qsizetype apayloadSize = static_cast<qsizetype>(evsize - 24);
                            if (apayloadSize >= 16) {
                                const float vol = readFloatLE(apayload + 12);
                                if (!std::isnan(vol) && !std::isinf(vol) && vol >= 0.0f)
                                    track.volume = static_cast<double>(vol);
                            }
                            if (apayloadSize >= 20) {
                                float pan = readFloatLE(apayload + 16);
                                if (pan == 0.0f && apayloadSize >= 24)
                                    pan = readFloatLE(apayload + 20);
                                if (!std::isnan(pan) && !std::isinf(pan))
                                    track.pan = qBound(-1.0, static_cast<double>(pan), 1.0);
                            }
                        } else if (evsize >= 40 && std::memcmp(bytes + evpos, kGuidW64List, 16) == 0
                                   && std::memcmp(bytes + evpos + 24, kListTrackEnvelopes, 16) == 0) {
                            qsizetype envPos = evpos + 40;
                            const qsizetype envEnd = evpos + static_cast<qsizetype>(evsize);
                            while (envPos + 24 <= envEnd) {
                                const uint8_t *envPtr = bytes + envPos;
                                const quint64 envSize = readU64LE(envPtr + 16);
                                if (envSize < 24 || envPos + static_cast<qsizetype>(envSize) > envEnd)
                                    break;
                                if (std::memcmp(envPtr, kChunkTrackEnvelope, 16) == 0) {
                                    parseTrackEnvelopeChunk(envPtr + 24, static_cast<qsizetype>(envSize - 24), track);
                                }
                                envPos += (envSize + 7) & ~7;
                            }
                        } else if (std::memcmp(bytes + evpos, kChunkTrackEnvelope, 16) == 0) {
                            parseTrackEnvelopeChunk(bytes + evpos + 24, static_cast<qsizetype>(evsize - 24), track);
                        } else if ((evsize >= 40 && std::memcmp(bytes + evpos, kGuidW64List, 16) == 0
                                    && (std::memcmp(bytes + evpos + 24, kListTrackAudioFx, 16) == 0
                                        || std::memcmp(bytes + evpos + 24, kListEventFx, 16) == 0))
                                   || std::memcmp(bytes + evpos, kListEventFx, 16) == 0
                                   || std::memcmp(bytes + evpos, kChunkEventEffect, 16) == 0) {
                            const uint8_t *fxPayload = bytes + evpos + 24;
                            const qsizetype fxPayloadSize = static_cast<qsizetype>(evsize - 24);
                            if (trackType == TrackType::Audio) {
                                for (const Effect &e : extractAudioEffects(fxPayload, fxPayloadSize))
                                    track.audioEffects.append(e);
                            } else {
                                for (const Effect &e : extractVideoEffects(fxPayload, fxPayloadSize))
                                    track.effects.append(e);
                            }
                        }
                        evpos += (evsize + 7) & ~7;
                    }

                    detectTransitionsForTrack(track);

                    if (!track.clips.isEmpty()) {
                        bool allText = true;
                        for (const auto &c : track.clips) {
                            if (c.type != ClipType::Text) {
                                allText = false;
                                break;
                            }
                        }
                        if (allText)
                            track.type = TrackType::Text;
                    }

                    project.tracks().append(track);
                }
                tpos += (tsize + 7) & ~7;
            }
            break;
        }
        pos += (chunkSize + 7) & ~7;
    }

    if (project.tracks().isEmpty()) {
        if (error)
            *error = QStringLiteral("No timeline tracks found in VEGAS project");
        return std::nullopt;
    }

    // Link clips via native VEGAS event groups (kTypeEventGroupList)
    for (const QList<quint32> &group : eventGroups) {
        if (group.size() < 2)
            continue;

        int matchedCount = 0;
        for (quint32 evId : group) {
            if (eventIdToClipLoc.contains(evId))
                ++matchedCount;
        }
        if (matchedCount < 2)
            continue;

        QString commonLinkId;
        bool hasVideo = false;
        bool hasAudio = false;

        for (quint32 evId : group) {
            if (!eventIdToClipLoc.contains(evId))
                continue;
            const auto loc = eventIdToClipLoc.value(evId);
            if (loc.first < project.tracks().size() && loc.second < project.tracks()[loc.first].clips.size()) {
                const Clip &c = project.tracks()[loc.first].clips[loc.second];
                if (!c.linkId.isEmpty()) {
                    commonLinkId = c.linkId;
                    break;
                }
            }
        }

        if (commonLinkId.isEmpty()) {
            commonLinkId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }

        for (quint32 evId : group) {
            if (!eventIdToClipLoc.contains(evId))
                continue;
            const auto loc = eventIdToClipLoc.value(evId);
            if (loc.first < project.tracks().size() && loc.second < project.tracks()[loc.first].clips.size()) {
                Clip &c = project.tracks()[loc.first].clips[loc.second];
                c.linkId = commonLinkId;
                if (project.tracks()[loc.first].type == TrackType::Video)
                    hasVideo = true;
                else if (project.tracks()[loc.first].type == TrackType::Audio)
                    hasAudio = true;
            }
        }

        if (hasVideo && hasAudio) {
            for (quint32 evId : group) {
                if (!eventIdToClipLoc.contains(evId))
                    continue;
                const auto loc = eventIdToClipLoc.value(evId);
                if (loc.first < project.tracks().size() && loc.second < project.tracks()[loc.first].clips.size()) {
                    if (project.tracks()[loc.first].type == TrackType::Video) {
                        project.tracks()[loc.first].clips[loc.second].suppressEmbeddedAudio = true;
                    }
                }
            }
        }
    }

    // Fallback: Symmetrically link companion video and audio clips sharing the same asset and timeline span
    for (Track &vTrack : project.tracks()) {
        if (vTrack.type != TrackType::Video)
            continue;
        for (Clip &vClip : vTrack.clips) {
            if (!vClip.linkId.isEmpty() || vClip.assetId.isEmpty())
                continue;
            for (Track &aTrack : project.tracks()) {
                if (aTrack.type != TrackType::Audio)
                    continue;
                for (Clip &aClip : aTrack.clips) {
                    if (!aClip.linkId.isEmpty() || aClip.assetId != vClip.assetId)
                        continue;
                    if (qAbs(aClip.timelineStart - vClip.timelineStart) <= 1000 &&
                        qAbs(aClip.timelineDuration - vClip.timelineDuration) <= 1000) {
                        const QString commonLinkId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                        vClip.linkId = commonLinkId;
                        aClip.linkId = commonLinkId;
                        if (vClip.type == ClipType::Composite)
                            vClip.suppressEmbeddedAudio = true;
                        break;
                    }
                }
                if (!vClip.linkId.isEmpty())
                    break;
            }
        }
    }

    // 4. Parse Timeline Markers and Regions (kTypeMarkerList and kGuidMarker)
    if (depth == 0) {
        qsizetype markerListStart = -1;
        qsizetype markerListEnd = -1;

        qsizetype lpos = 40;
        while (lpos + 24 <= totalSize) {
            const uint8_t *lptr = bytes + lpos;
            const quint64 chunkSize = readU64LE(lptr + 16);
            if (chunkSize < 24 || lpos + static_cast<qsizetype>(chunkSize) > totalSize)
                break;

            if (std::memcmp(lptr, kGuidW64List, 16) == 0 && lpos + 40 <= totalSize
                && std::memcmp(lptr + 24, kTypeMarkerList, 16) == 0) {
                markerListStart = lpos + 40;
                markerListEnd = lpos + static_cast<qsizetype>(chunkSize);
                break;
            }
            lpos += (chunkSize + 7) & ~7;
        }

        qsizetype scanPos = 40;
        while (scanPos + 16 <= totalSize) {
            const void *found = std::memchr(bytes + scanPos, kGuidMarker[0], totalSize - scanPos);
            if (!found)
                break;
            const qsizetype idx = static_cast<const uint8_t *>(found) - bytes;
            if (idx + 16 > totalSize)
                break;

            if (std::memcmp(bytes + idx, kGuidMarker, 16) == 0) {
                const bool inMarkerList = (markerListStart >= 0 && idx >= markerListStart && idx < markerListEnd);
                auto optMarker = parseMarkerPayload(bytes + idx + 16, totalSize - (idx + 16));
                if (optMarker.has_value()) {
                    const ParsedMarker &m = *optMarker;
                    const bool isTempo = m.name.startsWith(QLatin1String("Tempo: "));
                    if (!isTempo && (inMarkerList || !m.name.isEmpty())) {
                        if (m.isRegion) {
                            const QString baseLabel = m.name.isEmpty()
                                ? QStringLiteral("Region %1").arg(m.id)
                                : m.name;
                            Bookmark bmIn;
                            bmIn.timeUs = m.posUs;
                            bmIn.label = baseLabel;
                            project.bookmarks().append(bmIn);

                            if (m.durUs > 0) {
                                Bookmark bmOut;
                                bmOut.timeUs = m.posUs + m.durUs;
                                bmOut.label = QStringLiteral("%1 (Out)").arg(baseLabel);
                                project.bookmarks().append(bmOut);

                                Region region;
                                region.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                                region.startUs = m.posUs;
                                region.endUs = m.posUs + m.durUs;
                                region.label = baseLabel;
                                project.regions().append(region);
                            }
                        } else {
                            Bookmark bm;
                            bm.timeUs = m.posUs;
                            bm.label = m.name.isEmpty()
                                ? QStringLiteral("Marker %1").arg(m.id)
                                : m.name;
                            project.bookmarks().append(bm);
                        }
                    }
                }
            }
            scanPos = idx + 1;
        }

        if (!project.bookmarks().isEmpty()) {
            QList<Bookmark> uniqueBookmarks;
            for (const Bookmark &bm : project.bookmarks()) {
                bool exists = false;
                for (const Bookmark &u : uniqueBookmarks) {
                    if (u.timeUs == bm.timeUs && u.label == bm.label) {
                        exists = true;
                        break;
                    }
                }
                if (!exists)
                    uniqueBookmarks.append(bm);
            }
            std::sort(uniqueBookmarks.begin(), uniqueBookmarks.end(),
                      [](const Bookmark &a, const Bookmark &b) {
                          if (a.timeUs != b.timeUs)
                              return a.timeUs < b.timeUs;
                          return a.label < b.label;
                      });
            project.bookmarks() = std::move(uniqueBookmarks);
        }

        if (!project.regions().isEmpty()) {
            QList<Region> uniqueRegions;
            for (const Region &r : project.regions()) {
                bool exists = false;
                for (const Region &u : uniqueRegions) {
                    if (u.startUs == r.startUs && u.endUs == r.endUs && u.label == r.label) {
                        exists = true;
                        break;
                    }
                }
                if (!exists)
                    uniqueRegions.append(r);
            }
            std::stable_sort(uniqueRegions.begin(), uniqueRegions.end(),
                             [](const Region &a, const Region &b) {
                                 if (a.startUs != b.startUs)
                                     return a.startUs < b.startUs;
                                 return a.label < b.label;
                             });
            project.regions() = std::move(uniqueRegions);
        }

        for (const QString &seqId : project.sequenceIds()) {
            if (!project.openSequenceTabs().contains(seqId))
                project.openSequenceTabs().append(seqId);
        }
    }

    project.ensureTrackIds();
    return project;
}

} // namespace drift::vegas
