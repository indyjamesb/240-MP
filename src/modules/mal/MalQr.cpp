#include "MalQr.h"

#include "third_party/qrcodegen.hpp"

namespace mal {

QImage buildQr(const QString &text)
{
    try {
        const QByteArray utf8 = text.toUtf8();
        const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(
            utf8.constData(), qrcodegen::QrCode::Ecc::MEDIUM);
        const int modules = qr.getSize() + kQrQuietModules * 2;
        const int side = modules * kQrModulePixels;
        QImage image(side, side, QImage::Format_RGB32);
        if (image.isNull())
            return {};
        image.fill(Qt::white);
        for (int y = 0; y < qr.getSize(); ++y) {
            for (int x = 0; x < qr.getSize(); ++x) {
                if (!qr.getModule(x, y))
                    continue;
                const int x0 = (x + kQrQuietModules) * kQrModulePixels;
                const int y0 = (y + kQrQuietModules) * kQrModulePixels;
                for (int dy = 0; dy < kQrModulePixels; ++dy) {
                    auto *line = reinterpret_cast<QRgb *>(image.scanLine(y0 + dy));
                    for (int dx = 0; dx < kQrModulePixels; ++dx)
                        line[x0 + dx] = qRgb(0, 0, 0);
                }
            }
        }
        return image;
    } catch (const std::exception &) {
        return {};
    }
}

} // namespace mal
