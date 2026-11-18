#pragma once

// Pure playback decisions for the Plex module: when to transcode, which
// subtitle to start on, and whether that subtitle is burned into the
// transcode. No network and no config I/O, so the rules can be unit tested
// without standing up PlexBackend.
//
// Multi-version items are decided from Media[0] / Part[0] only. A later
// version the server also offers is ignored, matching the transcode request
// which always sends mediaIndex=0 and partIndex=0.

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1String>
#include <QList>
#include <QString>
#include <QStringList>

namespace plexplay {

// Used when a transcode is actually requested but Video Quality is still
// "auto" (the Direct Play label). That value is not a bitrate, and Plex
// rejects it as maxVideoBitrate. 4000 kbps is the existing "4 Mbps (720p)"
// preset: a Pi 4 can hardware-decode the resulting H.264, and it is a
// reasonable cap for a CRT. Video Quality still wins when it is a real cap.
inline constexpr int kFallbackBitrateKbps = 4000;

enum class TranscodeMode {
    Never,   // direct play everything (the historical "auto" behavior)
    Av1Only, // transcode only when the video codec is AV1
    All      // transcode every item (the historical numeric-bitrate behavior)
};

inline QString transcodeModeId(TranscodeMode mode) {
    switch (mode) {
    case TranscodeMode::Av1Only: return QStringLiteral("av1");
    case TranscodeMode::All:     return QStringLiteral("all");
    case TranscodeMode::Never:   break;
    }
    return QStringLiteral("never");
}

// True when Video Quality stores a kbps cap rather than Direct Play.
inline bool isPositiveBitrate(const QString &quality, int *kbps = nullptr) {
    bool ok = false;
    const int n = quality.trimmed().toInt(&ok);
    if (!ok || n <= 0) return false;
    if (kbps) *kbps = n;
    return true;
}

// `stored` is the transcode setting. Empty or unrecognized means the key was
// never written: a numeric Video Quality used to force every item through the
// transcoder, so that config keeps mode All. Anything else (including Direct
// Play / "auto") keeps mode Never.
inline TranscodeMode transcodeModeFromSettings(const QString &stored, const QString &videoQuality) {
    const QString s = stored.trimmed().toLower();
    if (s == QLatin1String("never")) return TranscodeMode::Never;
    if (s == QLatin1String("av1"))   return TranscodeMode::Av1Only;
    if (s == QLatin1String("all"))   return TranscodeMode::All;
    if (isPositiveBitrate(videoQuality)) return TranscodeMode::All;
    return TranscodeMode::Never;
}

// Plex reports the stream codec as "av1". ISOBMFF sample entries use "av01"
// (sometimes with a profile suffix, "av01.0.05M.08"). Match both, and don't
// treat an unrelated codec that merely starts with those letters as AV1.
inline bool isAv1Codec(const QString &codec) {
    const QString c = codec.trimmed().toLower();
    if (c == QLatin1String("av1") || c == QLatin1String("av01")) return true;
    if (c.startsWith(QLatin1String("av01"))) return true;
    if (c.startsWith(QLatin1String("av1."))
        || c.startsWith(QLatin1String("av1-"))
        || c.startsWith(QLatin1String("av1_"))) return true;
    return false;
}

inline bool shouldForceTranscode(TranscodeMode mode, const QString &videoCodec) {
    switch (mode) {
    case TranscodeMode::All:     return true;
    case TranscodeMode::Av1Only: return isAv1Codec(videoCodec);
    case TranscodeMode::Never:   return false;
    }
    return false;
}

// maxVideoBitrate query value. Always numeric — never "auto".
inline QString transcodeBitrateKbps(const QString &videoQuality) {
    int kbps = 0;
    if (isPositiveBitrate(videoQuality, &kbps))
        return QString::number(kbps);
    return QString::number(kFallbackBitrateKbps);
}

// Image subtitles have no text sidecar mpv can load. Plex's own names
// ("pgssub", "dvdsub") plus the ffmpeg names it sometimes forwards.
inline bool isImageSubtitleCodec(const QString &codec) {
    const QString c = codec.trimmed().toLower();
    return c == QLatin1String("pgssub")
        || c == QLatin1String("pgs")
        || c == QLatin1String("hdmv_pgs_subtitle")
        || c == QLatin1String("dvdsub")
        || c == QLatin1String("dvd_subtitle")
        || c == QLatin1String("vobsub")
        || c == QLatin1String("dvbsub")
        || c == QLatin1String("dvb_subtitle")
        || c == QLatin1String("idx");
}

// Whole-word, case-insensitive. "Signs & Songs", "Signs/Songs", "Signs" and
// "Songs" match; "designs" and a bare "song" do not.
inline bool containsWord(const QString &haystack, const QString &word) {
    const QString hay = haystack.toLower();
    const QString needle = word.toLower();
    if (needle.isEmpty()) return false;
    int from = 0;
    while (from < hay.size()) {
        const int at = hay.indexOf(needle, from);
        if (at < 0) return false;
        const bool leftOk = at == 0 || !hay.at(at - 1).isLetter();
        const int end = at + needle.size();
        const bool rightOk = end >= hay.size() || !hay.at(end).isLetter();
        if (leftOk && rightOk) return true;
        from = at + 1;
    }
    return false;
}

// 2 = both words, 1 = signs only, 0 = songs only, -1 = neither.
inline int signsSongsRank(const QString &title) {
    const bool signs = containsWord(title, QStringLiteral("signs"));
    const bool songs = containsWord(title, QStringLiteral("songs"));
    if (signs && songs) return 2;
    if (signs) return 1;
    if (songs) return 0;
    return -1;
}

inline bool isSignsSongsTitle(const QString &title) {
    return signsSongsRank(title) >= 0;
}

// On-screen name for a subtitle row. Plex's displayTitle is often just
// "English (ASS)" for every text track, which hides a Signs & Songs title.
// Prefer the extended name, and otherwise append the track title.
inline QString subtitleDisplayLabel(const QString &title, const QString &displayTitle,
                                    const QString &extendedDisplayTitle) {
    QString label = extendedDisplayTitle.trimmed();
    if (label.isEmpty()) {
        label = displayTitle.trimmed();
        const QString track = title.trimmed();
        if (!track.isEmpty() && !label.contains(track, Qt::CaseInsensitive))
            label = label.isEmpty() ? track : (label + QStringLiteral(" - ") + track);
    }
    if (label.isEmpty())
        label = QStringLiteral("UNKNOWN");
    return label.toUpper();
}

struct SubtitleCandidate {
    QString id;
    QString title;     // every title field Plex might have put the name in
    bool selected = false;
    bool image = false;
};

struct SubtitlePick {
    QString id = QStringLiteral("0"); // "0" is the synthetic Off track
    QString signsSongsId;             // empty when no signs/songs track exists
    bool image = false;               // the chosen track (id), not the signs one
};

// When preferSignsSongs is set and any track title matches, that track wins.
// A title that names both signs and songs beats "Signs" alone, which beats
// "Songs" alone. Ties keep the Plex-selected track, else the earliest one.
// Otherwise the pick is the Plex-selected track, or Off when Plex selected none.
// signsSongsId is filled either way so a later episode can reuse it.
inline SubtitlePick pickSubtitle(const QList<SubtitleCandidate> &tracks, bool preferSignsSongs) {
    SubtitlePick out;

    int bestRank = -1;
    bool bestWasSelected = false;

    for (const SubtitleCandidate &track : tracks) {
        if (track.id.isEmpty() || track.id == QLatin1String("0")) continue;
        if (track.selected && out.id == QLatin1String("0")) {
            out.id = track.id;
            out.image = track.image;
        }
        const int rank = signsSongsRank(track.title);
        if (rank < 0) continue;
        const bool better = out.signsSongsId.isEmpty()
            || rank > bestRank
            || (rank == bestRank && track.selected && !bestWasSelected);
        if (!better) continue;
        bestRank = rank;
        bestWasSelected = track.selected;
        out.signsSongsId = track.id;
        if (preferSignsSongs) {
            out.id = track.id;
            out.image = track.image;
        }
    }
    return out;
}

// Burn only a selected image subtitle. Text tracks are sidecars in mpv, and
// Off burns nothing. Burning is also skipped when we are not transcoding;
// callers AND this with the transcode decision.
inline bool shouldBurnSubtitle(const QString &subtitleId, bool imageSubtitle) {
    return imageSubtitle
        && !subtitleId.isEmpty()
        && subtitleId != QLatin1String("0");
}

struct PlaybackPrefs {
    QString transcode;
    QString videoQuality = QStringLiteral("auto");
    bool preferSignsSongs = true;
};

struct PlaybackPlan {
    TranscodeMode mode = TranscodeMode::Never;
    QString videoCodec;
    bool forceTranscode = false;
    QString selectedSubtitleId = QStringLiteral("0");
    QString signsSongsSubtitleId;
    bool selectedSubtitleImage = false;
    bool burnSubtitles = false;
    QString bitrateKbps = QString::number(kFallbackBitrateKbps);
};

// Codec is the first video stream on Media[0]'s first part, falling back to
// that media object's videoCodec when the stream itself has none. A second
// Media entry (another version of the same item) is not consulted.
inline PlaybackPlan planItemPlayback(const QJsonObject &meta, const PlaybackPrefs &prefs) {
    PlaybackPlan plan;
    plan.mode = transcodeModeFromSettings(prefs.transcode, prefs.videoQuality);
    plan.bitrateKbps = transcodeBitrateKbps(prefs.videoQuality);

    const QJsonArray mediaArr = meta.value(QStringLiteral("Media")).toArray();
    if (mediaArr.isEmpty()) {
        plan.forceTranscode = shouldForceTranscode(plan.mode, QString());
        return plan;
    }
    const QJsonObject media = mediaArr.at(0).toObject();
    const QJsonArray parts = media.value(QStringLiteral("Part")).toArray();
    const QJsonArray streams = parts.isEmpty()
        ? QJsonArray()
        : parts.at(0).toObject().value(QStringLiteral("Stream")).toArray();

    QString videoCodec;
    QList<SubtitleCandidate> subs;
    for (const QJsonValue &value : streams) {
        const QJsonObject stream = value.toObject();
        const int streamType = stream.value(QStringLiteral("streamType")).toInt();
        const QString codec = stream.value(QStringLiteral("codec")).toString();
        if (streamType == 1) {
            if (videoCodec.isEmpty()) videoCodec = codec;
            continue;
        }
        if (streamType != 3) continue;
        SubtitleCandidate candidate;
        candidate.id = QString::number(stream.value(QStringLiteral("id")).toInt());
        candidate.title = QStringList{
            stream.value(QStringLiteral("title")).toString(),
            stream.value(QStringLiteral("displayTitle")).toString(),
            stream.value(QStringLiteral("extendedDisplayTitle")).toString()
        }.join(QLatin1Char(' '));
        candidate.selected = stream.value(QStringLiteral("selected")).toBool();
        candidate.image = isImageSubtitleCodec(codec);
        subs.append(candidate);
    }
    if (videoCodec.isEmpty())
        videoCodec = media.value(QStringLiteral("videoCodec")).toString();

    plan.videoCodec = videoCodec;
    plan.forceTranscode = shouldForceTranscode(plan.mode, videoCodec);

    const SubtitlePick pick = pickSubtitle(subs, prefs.preferSignsSongs);
    plan.selectedSubtitleId = pick.id.isEmpty() ? QStringLiteral("0") : pick.id;
    plan.signsSongsSubtitleId = pick.signsSongsId;
    plan.selectedSubtitleImage = pick.image;
    plan.burnSubtitles = plan.forceTranscode
        && shouldBurnSubtitle(plan.selectedSubtitleId, pick.image);
    return plan;
}

} // namespace plexplay
