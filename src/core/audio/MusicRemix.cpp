#include "core/audio/MusicRemix.h"

#include <QtGlobal>

namespace drift {

TimeUs MusicRemixPlan::effectiveCrossfadeUs(int index) const
{
    if (index < 0 || index + 1 >= plan.size())
        return 0;
    const MusicRemixSegment &cur = plan.at(index);
    const MusicRemixSegment &next = plan.at(index + 1);
    const TimeUs maxCur = cur.rawDurationUs() / 3;
    const TimeUs maxNext = next.rawDurationUs() / 3;
    return qBound<TimeUs>(0, cur.crossfadeUs, qMin(maxCur, maxNext));
}

TimeUs MusicRemixPlan::segmentTimelineStartUs(int index) const
{
    if (index <= 0 || plan.isEmpty())
        return 0;
    TimeUs t = 0;
    const int limit = qMin(index, static_cast<int>(plan.size()) - 1);
    for (int i = 0; i < limit; ++i)
        t += plan.at(i).rawDurationUs() - effectiveCrossfadeUs(i);
    return t;
}

TimeUs MusicRemixPlan::totalDurationUs() const
{
    if (plan.isEmpty())
        return 0;
    TimeUs t = 0;
    for (int i = 0; i < plan.size(); ++i) {
        t += plan.at(i).rawDurationUs();
        if (i + 1 < plan.size())
            t -= effectiveCrossfadeUs(i);
    }
    return qMax<TimeUs>(0, t);
}

QList<TimeUs> MusicRemixPlan::jointTimelineOffsetsUs() const
{
    QList<TimeUs> out;
    if (plan.size() < 2)
        return out;
    out.reserve(plan.size() - 1);
    TimeUs t = 0;
    for (int i = 0; i + 1 < plan.size(); ++i) {
        const TimeUs xf = effectiveCrossfadeUs(i);
        const TimeUs segEnd = t + plan.at(i).rawDurationUs();
        out.append(segEnd - xf / 2);
        t = segEnd - xf;
    }
    return out;
}

MusicRemixPlan MusicRemixPlan::subRange(TimeUs startRelUs, TimeUs endRelUs) const
{
    MusicRemixPlan sub = *this;
    sub.plan.clear();
    if (!active() || endRelUs <= startRelUs) {
        sub.enabled = false;
        return sub;
    }

    TimeUs t = 0;
    for (int i = 0; i < plan.size(); ++i) {
        const MusicRemixSegment &seg = plan.at(i);
        const TimeUs segDur = seg.rawDurationUs();
        const TimeUs segStart = t;
        const TimeUs segEnd = t + segDur;
        const TimeUs hitStart = qMax(startRelUs, segStart);
        const TimeUs hitEnd = qMin(endRelUs, segEnd);
        if (hitEnd > hitStart) {
            MusicRemixSegment piece;
            piece.srcStartUs = seg.srcStartUs + (hitStart - segStart);
            piece.srcEndUs = seg.srcStartUs + (hitEnd - segStart);
            // Keep exit crossfade only if this segment wasn't truncated before its natural joint.
            piece.crossfadeUs = (hitEnd == segEnd && i + 1 < plan.size()) ? effectiveCrossfadeUs(i) : 0;
            sub.plan.append(piece);
        }
        if (i + 1 < plan.size())
            t = segEnd - effectiveCrossfadeUs(i);
    }
    if (!sub.plan.isEmpty())
        sub.plan.last().crossfadeUs = 0;
    sub.enabled = !sub.plan.isEmpty();
    sub.targetDurationUs = sub.totalDurationUs();
    return sub;
}

QJsonObject MusicRemixPlan::toJson() const
{
    QJsonArray segArray;
    for (const MusicRemixSegment &seg : plan) {
        segArray.append(QJsonObject{
            {QStringLiteral("srcStartUs"), qint64(seg.srcStartUs)},
            {QStringLiteral("srcEndUs"), qint64(seg.srcEndUs)},
            {QStringLiteral("crossfadeUs"), qint64(seg.crossfadeUs)},
        });
    }
    return QJsonObject{
        {QStringLiteral("enabled"), enabled},
        {QStringLiteral("targetDurationUs"), qint64(targetDurationUs)},
        {QStringLiteral("originalDurationUs"), qint64(originalDurationUs)},
        {QStringLiteral("originalSrcOutUs"), qint64(originalSrcOutUs)},
        {QStringLiteral("segments"), segments},
        {QStringLiteral("variations"), variations},
        {QStringLiteral("bpm"), bpm},
        {QStringLiteral("plan"), segArray},
    };
}

MusicRemixPlan MusicRemixPlan::fromJson(const QJsonObject &obj)
{
    MusicRemixPlan out;
    if (obj.isEmpty())
        return out;
    out.enabled = obj.value(QStringLiteral("enabled")).toBool(false);
    out.targetDurationUs = TimeUs(obj.value(QStringLiteral("targetDurationUs")).toInteger(0));
    out.originalDurationUs = TimeUs(obj.value(QStringLiteral("originalDurationUs")).toInteger(0));
    out.originalSrcOutUs = TimeUs(obj.value(QStringLiteral("originalSrcOutUs")).toInteger(0));
    out.segments = qBound(0, obj.value(QStringLiteral("segments")).toInt(50), 100);
    out.variations = qBound(0, obj.value(QStringLiteral("variations")).toInt(50), 100);
    out.bpm = obj.value(QStringLiteral("bpm")).toDouble(0.0);

    const QJsonArray segArray = obj.value(QStringLiteral("plan")).toArray();
    out.plan.reserve(segArray.size());
    for (const QJsonValue &v : segArray) {
        const QJsonObject segObj = v.toObject();
        MusicRemixSegment seg;
        seg.srcStartUs = TimeUs(segObj.value(QStringLiteral("srcStartUs")).toInteger(0));
        seg.srcEndUs = TimeUs(segObj.value(QStringLiteral("srcEndUs")).toInteger(0));
        seg.crossfadeUs = qMax<TimeUs>(0, TimeUs(segObj.value(QStringLiteral("crossfadeUs")).toInteger(20000)));
        if (seg.srcEndUs > seg.srcStartUs)
            out.plan.append(seg);
    }
    if (out.plan.isEmpty())
        out.enabled = false;
    return out;
}

} // namespace drift
