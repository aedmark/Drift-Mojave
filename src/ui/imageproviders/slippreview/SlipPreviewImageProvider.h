#pragma once

#include <QQuickImageProvider>

// Serves the 4-Up Slip Monitor frames from SlipPreviewImageStore
// as `image://slippreview/<slot>?rev=<n>` where slot is 0..3.
class SlipPreviewImageProvider : public QQuickImageProvider
{
public:
    SlipPreviewImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
