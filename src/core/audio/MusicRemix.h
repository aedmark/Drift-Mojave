#pragma once

#include "core/base/Time.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>

namespace drift {

// A contiguous slice of source audio [srcStartUs, srcEndUs) placed sequentially in a remixed clip.
// Adjacent segments overlap across `crossfadeUs` on the timeline so beat-matched cuts blend smoothly
// with an equal-power curve (cos/sin) without reading outside either segment's source range.
struct MusicRemixSegment
{
    TimeUs srcStartUs = 0;
    TimeUs srcEndUs = 0;
    // Overlap duration into the *next* segment in the plan; ignored on the final segment.
    TimeUs crossfadeUs = 20000; // 20 ms default equal-power crossfade

    TimeUs rawDurationUs() const { return qMax<TimeUs>(0, srcEndUs - srcStartUs); }

    bool operator==(const MusicRemixSegment &) const = default;
};

// Beat-matched re-sequencing plan for an audio clip (Filmcraft-inspired Music Auto-Remix).
// Stores exact source segments, target metadata, and the clip's pre-remix bounds so the user can
// preview, adjust target duration/cut density, or revert cleanly with undo support.
struct MusicRemixPlan
{
    bool enabled = false;
    TimeUs targetDurationUs = 0;
    TimeUs originalDurationUs = 0;
    TimeUs originalSrcOutUs = 0;
    int segments = 50;   // 0..100: fewer long sections vs more structural cuts
    int variations = 50; // 0..100: conservative harmonic matches vs exploratory transitions
    double bpm = 0.0;
    QList<MusicRemixSegment> plan;

    bool active() const { return enabled && !plan.isEmpty(); }
    void clear() { *this = MusicRemixPlan{}; }

    // Effective equal-power overlap between segment `index` and `index + 1`, clamped so neither
    // segment shrinks below zero or inverts.
    TimeUs effectiveCrossfadeUs(int index) const;

    // Start offset of segment `index` in clip-relative timeline microseconds.
    TimeUs segmentTimelineStartUs(int index) const;

    // Total remixed clip timeline duration in microseconds.
    TimeUs totalDurationUs() const;

    // Cut joint midpoints in clip-relative timeline microseconds (size == max(0, plan.size() - 1)).
    QList<TimeUs> jointTimelineOffsetsUs() const;

    // Slices this remix plan to the clip-relative sub-window [startRelUs, endRelUs).
    MusicRemixPlan subRange(TimeUs startRelUs, TimeUs endRelUs) const;

    QJsonObject toJson() const;
    static MusicRemixPlan fromJson(const QJsonObject &obj);

    bool operator==(const MusicRemixPlan &) const = default;
};

} // namespace drift
