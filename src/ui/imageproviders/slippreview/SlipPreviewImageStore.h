#pragma once

#include <QImage>

// Decoded frames for the 4-Up Slip Monitor overlay (0 = previous clip Out,
// 1 = slipped clip In, 2 = slipped clip Out, 3 = next clip In).
//
// Split out from SlipPreviewImageProvider so model-layer tests (`tst_editorstate`)
// can write here without linking Qt Quick.
namespace SlipPreviewImageStore {

enum Slot {
    PrevOut = 0,
    ClipIn = 1,
    ClipOut = 2,
    NextIn = 3,
    SlotCount = 4
};

void setQuad(int slot, const QImage &frame);
void clear();

QImage quad(int slot);

} // namespace SlipPreviewImageStore
