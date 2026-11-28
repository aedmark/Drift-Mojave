#pragma once

#include <QString>

namespace drift {

// Runs the headless playback/compositor/audio benchmark suite (`drift --headless --bench`)
// and optionally writes machine-readable JSON to `jsonOutPath`. Returns process exit code.
int runHeadlessBench(const QString &jsonOutPath);

} // namespace drift
