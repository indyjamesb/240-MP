#pragma once

// Reader for anime-mappings.bin.gz from https://github.com/edde746/anime-mappings.
// The byte layout is specified in that repository's README (format version 1).
// Plezy decodes the same asset; this reader follows the format, and skips
// tables or columns it does not use so a later file of version 1 can add them.

#include "MalIndex.h"

#include <QByteArray>

namespace mal {

// Throws std::runtime_error when the bytes are not a version-1 packed list.
AnimeMappings decodeAnimeMappings(const QByteArray &gzipOrRaw);

} // namespace mal
