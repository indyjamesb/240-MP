#pragma once

// Playback samples that already count as a library timeline report.
//
// Plex VOD calls this from update_timeline. Emby and Jellyfin call it from
// their Playing/Progress and Playing/Stopped reports. Anything that
// deliberately reports no timeline — shuffle / jukebox playback, Plex live
// TV (update_live_timeline), and a channels module that does not post a
// timeline — must not call this, and therefore does not scrobble.

#include <QString>
#include <QtGlobal>

namespace mal {

struct ProgressSample {
    const char *service = "";
    QString itemId;
    qint64 positionMs = 0;
    qint64 durationMs = 0;
};

class ProgressSink {
public:
    virtual ~ProgressSink() = default;
    virtual void observe(const ProgressSample &sample) = 0;
};

void setProgressSink(ProgressSink *sink);
void observeProgress(const ProgressSample &sample);

} // namespace mal
