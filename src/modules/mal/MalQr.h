#pragma once

#include <QImage>
#include <QString>

namespace mal {

// Quiet zone around the modules, in modules. Each module is drawn as a
// square of kQrModulePixels pixels so the edges stay on whole pixels.
constexpr int kQrQuietModules = 4;
constexpr int kQrModulePixels = 4;

// Black modules on a white field, quiet zone included. Null when the text
// does not fit a QR symbol. MalBackend::buildQr publishes this as a PNG
// data URL; QML shows that image instead of painting a Canvas (Canvas
// requestPaint is dropped under EGLFS when the item is still zero-size).
QImage buildQr(const QString &text);

} // namespace mal
