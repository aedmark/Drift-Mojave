#include "ui/imageproviders/slippreview/SlipPreviewImageProvider.h"

#include "ui/imageproviders/slippreview/SlipPreviewImageStore.h"

SlipPreviewImageProvider::SlipPreviewImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage SlipPreviewImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    bool ok = false;
    const int slot = id.section(QLatin1Char('?'), 0, 0).toInt(&ok);

    QImage image = ok ? SlipPreviewImageStore::quad(slot) : QImage();
    if (image.isNull()) {
        image = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
    }

    if (requestedSize.isValid() && requestedSize.width() > 0 && requestedSize.height() > 0)
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    if (size)
        *size = image.size();
    return image;
}
