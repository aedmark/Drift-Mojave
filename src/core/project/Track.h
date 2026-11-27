#pragma once

#include "core/project/clip/Clip.h"
#include "core/project/Transition.h"

#include <QList>
#include <QString>

namespace drift {

enum class TrackType { Video, Audio, Text, Subtitle, Shape, Adjustment };

QString trackTypeToString(TrackType type);
TrackType trackTypeFromString(const QString &type);

// What an adjustment track applies to. AllBelow is the standalone case: it snapshots the
// canvas composited so far, so it treats every track under it as one flattened image.
// ParentTrack is the nested lane: it contributes its effects to each clip of `parentTrackId`
// it overlaps, inside that clip's own layer pass, so the clip's transform still carries them.
// Range is a transform layer: its Transform clips parent every track from just below it down to
// `spanEndTrackId`.
// Camera is the scene camera: its Camera clips set the viewpoint every track in the sequence is
// seen through. Unlike AllBelow it never snapshots or flattens the canvas — it contributes a
// matrix and nothing else — and unlike Range it covers no span, because a scene has one viewer.
// There is at most one of these per sequence; normalizeCameraLayers enforces that.
enum class AdjustmentScope { AllBelow, ParentTrack, Range, Camera };

QString adjustmentScopeToString(AdjustmentScope scope);
AdjustmentScope adjustmentScopeFromString(const QString &scope);

// Parallel aux / FX send from an audio-capable track to an AudioBus.
struct AudioSend
{
    QString busId;
    double level = 0.0; // Linear gain in [0.0, 2.0].
    bool preFader = false;
    bool enabled = true;

    bool operator==(const AudioSend &) const = default;
};

// Project-level audio submix / FX bus routed to Master (or another bus via outputBusId).
struct AudioBus
{
    QString id;
    QString name;
    double volume = 1.0; // Linear gain in [0.0, 2.0].
    double pan = 0.0;    // -1.0 (left) .. 1.0 (right).
    bool muted = false;
    bool solo = false;
    QString outputBusId; // Empty = Master; non-empty = another AudioBus::id (cycle-guarded).
    QList<Effect> audioEffects;
};

// Persistent 2D / 3D track-wide motion transform applied to all clips on a video/overlay track.
struct TrackMotion
{
    bool enabled = false;
    double posX = 0.0;    // Normalized canvas-width offset (-1.0 .. 1.0).
    double posY = 0.0;    // Normalized canvas-height offset (-1.0 .. 1.0).
    double scaleX = 1.0;
    double scaleY = 1.0;
    double rotationDeg = 0.0;
    double rotationXDeg = 0.0;
    double rotationYDeg = 0.0;
    double zOffset = 0.0; // Normalized 3D Z depth offset.
    double opacity = 1.0; // Track-wide alpha multiplier [0.0, 1.0].

    KeyframeTrack<double> posXTrack;
    KeyframeTrack<double> posYTrack;
    KeyframeTrack<double> scaleXTrack;
    KeyframeTrack<double> scaleYTrack;
    KeyframeTrack<double> rotationTrack;
    KeyframeTrack<double> opacityTrack;

    bool isDefault() const
    {
        return !enabled && qFuzzyIsNull(posX) && qFuzzyIsNull(posY)
               && qFuzzyCompare(scaleX, 1.0) && qFuzzyCompare(scaleY, 1.0)
               && qFuzzyIsNull(rotationDeg) && qFuzzyIsNull(rotationXDeg)
               && qFuzzyIsNull(rotationYDeg) && qFuzzyIsNull(zOffset)
               && qFuzzyCompare(opacity, 1.0) && posXTrack.isEmpty()
               && posYTrack.isEmpty() && scaleXTrack.isEmpty()
               && scaleYTrack.isEmpty() && rotationTrack.isEmpty()
               && opacityTrack.isEmpty();
    }
};

struct Track
{
    // Stable across reorders, unlike the array index. Nested lanes point at their parent by
    // this, so moving a track cannot silently reparent them. Minted on load for v3 projects.
    QString id;
    TrackType type = TrackType::Video;
    QList<Clip> clips;
    QList<Transition> transitions;
    QList<Effect> audioEffects; // Persistent Track FX rack (processes summed audio of the track)
    // User-given label, shown instead of the default one ("Video 1") once set.
    // Empty by default — most tracks never get a custom name.
    QString name;
    // The number in the default label ("Video 2"), kept by the track so adding, deleting or
    // reordering tracks never renumbers the others. Counted per kind (trackNumberKind) and given
    // out by ensureTrackIds; 0 for the kinds whose label has no number (nested lanes, camera).
    int number = 0;
    bool muted = false;
    bool hidden = false;
    bool locked = false;
    bool solo = false;
    // Shown and heard while editing, but never exported (a review lane: a VO script to read along,
    // notes). Export and composite flattening render Project::exportView, which drops it.
    bool excludeFromExport = false;
    // Set on a track a project-folder script created; see Clip::generatedBy.
    QString generatedBy;
    double volume = 1.0;
    double pan = 0.0;
    // Timeline-time track automation envelopes (VEGAS parity).
    KeyframeTrack<double> volumeEnvelope;
    KeyframeTrack<double> panEnvelope;
    KeyframeTrack<double> compositeEnvelope;
    KeyframeTrack<double> muteEnvelope;
    // View-only flags: which track automation envelopes are shown in the timeline lane.
    bool showVolumeEnvelope = false;
    bool showPanEnvelope = false;
    bool showCompositeEnvelope = false;
    bool showMuteEnvelope = false;
    // view-only, video tracks: how clips draw. Video = filmstrip only, Both = filmstrip with a
    // waveform bar along the bottom, Waveform = waveform instead of the filmstrip.
    enum class ClipDisplay { Video = 0, Both = 1, Waveform = 2 };
    ClipDisplay clipDisplay = ClipDisplay::Both;
    // view-only: draw a multi-channel clip's waveform as one lane per source channel instead
    // of the single max-across-channels envelope. Opt-in because a 5.1 clip in a standard row
    // gives each lane a few pixels — pair it with heightScale.
    bool showChannelWaveforms = false;
    // view-only: multiplies this track's base row height so a single lane can be
    // enlarged (waveform editing) without zooming the whole timeline.
    qreal heightScale = 1.0;
    // view-only, parent tracks: nested adjustment lanes fold into thin coverage ribbons.
    bool lanesCollapsed = false;
    // Collapsible track folders / groups (Tier 7E).
    QString folderName;
    bool folderCollapsed = false;

    // Adjustment tracks only.
    AdjustmentScope adjustmentScope = AdjustmentScope::AllBelow;
    // Set iff type == Adjustment && adjustmentScope == ParentTrack.
    QString parentTrackId;
    // Range tracks only: the last track the layer covers. Kept when that track is moved above
    // the layer, so moving it back restores the group.
    QString spanEndTrackId;

    // Audio routing (Tier 2): empty outputBusId routes directly to Master.
    QString outputBusId;
    QList<AudioSend> audioSends;

    // Video track FX, track motion & parent/child compositing (Tier 2).
    QList<Effect> effects;
    TrackMotion motion;
    QString compositeParentTrackId;
    BlendMode compositeBlendMode = BlendMode::Normal;

    bool allowsClipType(ClipType clipType) const;
    // allowsClipType plus the adjustment kind: a Range track holds Transform clips and nothing
    // else, a Camera track holds Camera clips and nothing else, and neither kind lives anywhere
    // but on its own sort of track.
    bool acceptsClip(const Clip &clip) const;

    bool isAdjustment() const { return type == TrackType::Adjustment; }
    // A nested lane draws inside its parent's row rather than occupying one of its own.
    bool isAdjustmentLane() const
    {
        return type == TrackType::Adjustment && adjustmentScope == AdjustmentScope::ParentTrack
               && !parentTrackId.isEmpty();
    }
    bool isTransformLayer() const
    {
        return type == TrackType::Adjustment && adjustmentScope == AdjustmentScope::Range;
    }
    bool isCameraLayer() const
    {
        return type == TrackType::Adjustment && adjustmentScope == AdjustmentScope::Camera;
    }
};

// What a track's number counts among: its type, with transform layers counted apart from other
// adjustment tracks. Empty for nested lanes and the camera layer, whose labels have no number.
QString trackNumberKind(const Track &track);

// The track's name, else its default label in English ("Video 2", "Transform 1", "Camera", or a
// nested lane's "Effects" / "Audio FX" / "Mask"), as the timeline shows it. For replies to agents; the UI builds the same label translated.
QString trackDefaultLabel(const Track &track);

// True when any track in the list is soloed. While one is, the mixer plays only soloed tracks.
bool anyTrackSoloed(const QList<Track> &tracks);

// Whether the mixer plays this track's own audio: not muted, not hidden, and either nothing is
// soloed or this track is. Every place that decides what is heard uses this, so the flags can't
// drift apart again. Adjustment tracks are not subject to it: they carry no audio of their own.
inline bool trackAudible(const Track &track, bool anySolo)
{
    return !track.muted && !track.hidden && (!anySolo || track.solo);
}

} // namespace drift
