#pragma once

#include <QString>
#include <QStringList>

namespace mal {

// Plezy defaults to a blacklist with no ids, which tracks every library.
// 240-MP stores the enabled Plex section ids instead. The key is absent until
// the user changes a toggle (track all). Once present, only those ids track,
// and an empty list tracks nothing.
struct LibrarySelection {
    bool configured = false;
    QStringList ids;
};

inline bool libraryIsTracked(const LibrarySelection &selection, const QString &sectionId)
{
    if (!selection.configured)
        return true;
    if (sectionId.isEmpty())
        return false;
    return selection.ids.contains(sectionId);
}

// setPhase keeps the previous status when the new one is null. A default
// QString() is null, so clearing the line takes an empty string that is not.
inline QString clearedStatus()
{
    return QStringLiteral("");
}

// A second OAuth callback (phone reload, prefetch) must not start another
// code exchange or paint a failure over a session that already saved.
struct ExchangeGate {
    bool inFlight = false;
    bool linked = false;

    bool accept() const { return !inFlight && !linked; }
    bool allowFailureUi() const { return !linked; }
};

inline bool sessionReplyCurrent(int captured, int current)
{
    return captured == current;
}

} // namespace mal
