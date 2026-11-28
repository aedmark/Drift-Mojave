#include "ui/imageproviders/slippreview/SlipPreviewImageStore.h"

#include <QMutex>
#include <QMutexLocker>

#include <array>

namespace {
QMutex s_mutex;
std::array<QImage, SlipPreviewImageStore::SlotCount> s_quads;
}

namespace SlipPreviewImageStore {

void setQuad(int slot, const QImage &frame)
{
    if (slot < 0 || slot >= SlotCount)
        return;
    QMutexLocker lock(&s_mutex);
    s_quads[size_t(slot)] = frame;
}

void clear()
{
    QMutexLocker lock(&s_mutex);
    for (QImage &img : s_quads)
        img = QImage();
}

QImage quad(int slot)
{
    if (slot < 0 || slot >= SlotCount)
        return {};
    QMutexLocker lock(&s_mutex);
    return s_quads[size_t(slot)];
}

} // namespace SlipPreviewImageStore
