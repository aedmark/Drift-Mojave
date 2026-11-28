#pragma once

#include "core/audio/MusicRemix.h"
#include "core/base/Time.h"

#include <QList>
#include <QString>

namespace drift {

struct AudioRemixOptions
{
    TimeUs targetDurationUs = 0;
    int segments = 50;   // 0..100: fewer longer sections vs more structural cuts
    int variations = 50; // 0..100: strict harmonic match vs more transition options
};

struct AudioRemixResult
{
    bool ok = false;
    QString error;
    MusicRemixPlan plan;
    TimeUs actualDurationUs = 0;
    double avgSimilarity = 0.0;
};

// Beat-matched music auto-remix planner built on AudioOnsets + drift::Stft.
// Extracts a beat grid and per-beat chroma + log-spectral timbre features, scores 4-beat context
// transitions across beat/bar boundaries, and plans a cut sequence matching `targetDurationUs`
// within ~1 beat while preserving intro/outro phrasing and smoothing joints with 20 ms equal-power
// crossfades.
class AudioRemix
{
public:
    static constexpr int kAnalysisSampleRate = 22050;
    static constexpr TimeUs kDefaultCrossfadeUs = 20000;

    // Plans from a mono PCM buffer covering [srcInUs, srcOutUs). Pure & deterministic; used by
    // unit tests and after media decoding.
    static AudioRemixResult planFromMonoPcm(const float *mono, int frameCount, int sampleRate,
                                            TimeUs srcInUs, TimeUs srcOutUs,
                                            const AudioRemixOptions &options);

    // Convenience entry point for a media file: decodes mono audio once (or hits the internal
    // feature cache for `(canonicalPath, streamIndex, srcInUs, srcOutUs)`) and plans immediately.
    static AudioRemixResult planFromMedia(const QString &path, int streamIndex,
                                          TimeUs srcInUs, TimeUs srcOutUs,
                                          const AudioRemixOptions &options);

    // Clears cached decoded beat/feature analysis (mostly for tests or memory pressure).
    static void clearCache();
};

} // namespace drift
