#include "PlexPlayback.h"

#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>

using namespace plexplay;

namespace {

QJsonObject videoStream(int id, const char *codec) {
    return QJsonObject{
        {QStringLiteral("id"), id},
        {QStringLiteral("streamType"), 1},
        {QStringLiteral("codec"), QString::fromLatin1(codec)},
    };
}

QJsonObject subtitleStream(int id, const char *codec, const char *title, bool selected,
                           const char *extended = "") {
    QJsonObject stream{
        {QStringLiteral("id"), id},
        {QStringLiteral("streamType"), 3},
        {QStringLiteral("codec"), QString::fromLatin1(codec)},
        {QStringLiteral("title"), QString::fromLatin1(title)},
        {QStringLiteral("displayTitle"), QString::fromLatin1("English")},
        {QStringLiteral("selected"), selected},
    };
    if (extended[0] != '\0')
        stream.insert(QStringLiteral("extendedDisplayTitle"), QString::fromLatin1(extended));
    return stream;
}

QJsonObject itemWithMedia(const QJsonArray &media) {
    return QJsonObject{{QStringLiteral("Media"), media}};
}

QJsonObject mediaWithStreams(const QJsonArray &streams, const char *videoCodec = "") {
    QJsonObject media{
        {QStringLiteral("Part"), QJsonArray{
            QJsonObject{{QStringLiteral("Stream"), streams}}
        }},
    };
    if (videoCodec[0] != '\0')
        media.insert(QStringLiteral("videoCodec"), QString::fromLatin1(videoCodec));
    return media;
}

PlaybackPrefs prefsFor(const char *mode, const char *quality, bool signs = true) {
    PlaybackPrefs prefs;
    prefs.transcode = QString::fromLatin1(mode);
    prefs.videoQuality = QString::fromLatin1(quality);
    prefs.preferSignsSongs = signs;
    return prefs;
}

} // namespace

class PlexPlaybackTests : public QObject {
    Q_OBJECT
private slots:
    void transcodeModeMigratesNumericQualityToAll();
    void transcodeModeExplicitValueWins();
    void av1CodecNames();
    void av1OnlyTranscodesAv1AndDirectPlaysTheRest();
    void neverAndAllIgnoreCodec();
    void bitrateFallsBackWhenQualityIsDirectPlay();
    void signsSongsTitles();
    void subtitleLabelKeepsTheTrackTitle();
    void signsSongsBeatsThePlexSelection();
    void signsSongsRanksBothWordsAboveOne();
    void signsSongsOffKeepsThePlexSelection();
    void textSubsAreNotBurnedImageSubsAre();
    void secondMediaVersionIsIgnored();
    void streamCodecBeatsMediaCodec();
    void missingStreamCodecUsesMediaCodec();
};

void PlexPlaybackTests::transcodeModeMigratesNumericQualityToAll() {
    QCOMPARE(transcodeModeFromSettings(QString(), QStringLiteral("4000")), TranscodeMode::All);
    QCOMPARE(transcodeModeFromSettings(QString(), QStringLiteral("8000")), TranscodeMode::All);
    QCOMPARE(transcodeModeFromSettings(QString(), QStringLiteral("2000")), TranscodeMode::All);
    QCOMPARE(transcodeModeFromSettings(QStringLiteral("  "), QStringLiteral("4000")), TranscodeMode::All);
    // Unrecognized stored value is treated as unset, so the old behavior remains.
    QCOMPARE(transcodeModeFromSettings(QStringLiteral("bogus"), QStringLiteral("4000")), TranscodeMode::All);
    QCOMPARE(transcodeModeId(TranscodeMode::All), QStringLiteral("all"));

    QCOMPARE(transcodeModeFromSettings(QString(), QStringLiteral("auto")), TranscodeMode::Never);
    QCOMPARE(transcodeModeFromSettings(QString(), QString()), TranscodeMode::Never);
    QCOMPARE(transcodeModeFromSettings(QStringLiteral("bogus"), QStringLiteral("auto")), TranscodeMode::Never);
    QCOMPARE(transcodeModeId(TranscodeMode::Never), QStringLiteral("never"));
}

void PlexPlaybackTests::transcodeModeExplicitValueWins() {
    QCOMPARE(transcodeModeFromSettings(QStringLiteral("never"), QStringLiteral("8000")), TranscodeMode::Never);
    QCOMPARE(transcodeModeFromSettings(QStringLiteral("AV1"), QStringLiteral("8000")), TranscodeMode::Av1Only);
    QCOMPARE(transcodeModeFromSettings(QStringLiteral(" av1 "), QStringLiteral("auto")), TranscodeMode::Av1Only);
    QCOMPARE(transcodeModeFromSettings(QStringLiteral("all"), QStringLiteral("auto")), TranscodeMode::All);
    QCOMPARE(transcodeModeFromSettings(QStringLiteral("  All "), QStringLiteral("auto")), TranscodeMode::All);
    QCOMPARE(transcodeModeId(TranscodeMode::Av1Only), QStringLiteral("av1"));
}

void PlexPlaybackTests::av1CodecNames() {
    QVERIFY(isAv1Codec(QStringLiteral("av1")));
    QVERIFY(isAv1Codec(QStringLiteral("AV1")));
    QVERIFY(isAv1Codec(QStringLiteral(" av1 ")));
    QVERIFY(isAv1Codec(QStringLiteral("av01")));
    QVERIFY(isAv1Codec(QStringLiteral("av01.0.05M.08")));
    QVERIFY(isAv1Codec(QStringLiteral("AV01.0.08M.10")));
    QVERIFY(!isAv1Codec(QStringLiteral("h264")));
    QVERIFY(!isAv1Codec(QStringLiteral("hevc")));
    QVERIFY(!isAv1Codec(QStringLiteral("vp9")));
    QVERIFY(!isAv1Codec(QStringLiteral("av1x")));
    QVERIFY(!isAv1Codec(QString()));
}

void PlexPlaybackTests::av1OnlyTranscodesAv1AndDirectPlaysTheRest() {
    const QJsonObject av1 = itemWithMedia(QJsonArray{
        mediaWithStreams(QJsonArray{videoStream(1, "av1")})
    });
    const QJsonObject av01 = itemWithMedia(QJsonArray{
        mediaWithStreams(QJsonArray{videoStream(1, "av01")})
    });
    const QJsonObject h264 = itemWithMedia(QJsonArray{
        mediaWithStreams(QJsonArray{videoStream(1, "h264")})
    });
    const PlaybackPrefs av1Only = prefsFor("av1", "auto");

    QVERIFY(planItemPlayback(av1, av1Only).forceTranscode);
    QVERIFY(planItemPlayback(av01, av1Only).forceTranscode);
    QVERIFY(!planItemPlayback(h264, av1Only).forceTranscode);
    QCOMPARE(planItemPlayback(h264, av1Only).videoCodec, QStringLiteral("h264"));
    // No codec at all cannot be shown to be AV1, so it direct-plays.
    QVERIFY(!planItemPlayback(QJsonObject{}, av1Only).forceTranscode);
}

void PlexPlaybackTests::neverAndAllIgnoreCodec() {
    const QJsonObject av1 = itemWithMedia(QJsonArray{
        mediaWithStreams(QJsonArray{videoStream(1, "av1")})
    });
    QVERIFY(!planItemPlayback(av1, prefsFor("never", "8000")).forceTranscode);
    QVERIFY(planItemPlayback(av1, prefsFor("all", "auto")).forceTranscode);
    const QJsonObject h264 = itemWithMedia(QJsonArray{
        mediaWithStreams(QJsonArray{videoStream(1, "h264")})
    });
    QVERIFY(planItemPlayback(h264, prefsFor("all", "2000")).forceTranscode);
    QVERIFY(!planItemPlayback(h264, prefsFor("never", "auto")).forceTranscode);
}

void PlexPlaybackTests::bitrateFallsBackWhenQualityIsDirectPlay() {
    QCOMPARE(transcodeBitrateKbps(QStringLiteral("auto")), QStringLiteral("4000"));
    QCOMPARE(transcodeBitrateKbps(QString()), QStringLiteral("4000"));
    QCOMPARE(transcodeBitrateKbps(QStringLiteral("0")), QStringLiteral("4000"));
    QCOMPARE(transcodeBitrateKbps(QStringLiteral("-5")), QStringLiteral("4000"));
    QCOMPARE(transcodeBitrateKbps(QStringLiteral("abc")), QStringLiteral("4000"));
    QCOMPARE(transcodeBitrateKbps(QStringLiteral("8000")), QStringLiteral("8000"));
    QCOMPARE(transcodeBitrateKbps(QStringLiteral(" 2000 ")), QStringLiteral("2000"));

    const QJsonObject av1 = itemWithMedia(QJsonArray{
        mediaWithStreams(QJsonArray{videoStream(1, "av1")})
    });
    // AV1 only + Direct Play still produces a numeric cap, not "auto".
    QCOMPARE(planItemPlayback(av1, prefsFor("av1", "auto")).bitrateKbps, QStringLiteral("4000"));
    QCOMPARE(planItemPlayback(av1, prefsFor("all", "8000")).bitrateKbps, QStringLiteral("8000"));
    QCOMPARE(kFallbackBitrateKbps, 4000);
}

void PlexPlaybackTests::signsSongsTitles() {
    QVERIFY(isSignsSongsTitle(QStringLiteral("Signs & Songs")));
    QVERIFY(isSignsSongsTitle(QStringLiteral("signs/songs")));
    QVERIFY(isSignsSongsTitle(QStringLiteral("Signs")));
    QVERIFY(isSignsSongsTitle(QStringLiteral("Songs")));
    QVERIFY(isSignsSongsTitle(QStringLiteral("ENGLISH - SIGNS & SONGS")));
    QVERIFY(isSignsSongsTitle(QStringLiteral("English (Signs & Songs)")));
    QVERIFY(isSignsSongsTitle(QStringLiteral("Forced Signs")));
    QVERIFY(isSignsSongsTitle(QStringLiteral("Opening Songs")));
    QVERIFY(!isSignsSongsTitle(QStringLiteral("Full Dialogue")));
    QVERIFY(!isSignsSongsTitle(QStringLiteral("English (ASS)")));
    QVERIFY(!isSignsSongsTitle(QStringLiteral("designs")));
    QVERIFY(!isSignsSongsTitle(QStringLiteral("song")));
    QVERIFY(!isSignsSongsTitle(QStringLiteral("sign")));
    QVERIFY(!isSignsSongsTitle(QString()));
}

void PlexPlaybackTests::subtitleLabelKeepsTheTrackTitle() {
    QCOMPARE(subtitleDisplayLabel(QStringLiteral("Signs & Songs"),
                                  QStringLiteral("English (ASS)"),
                                  QString()),
             QStringLiteral("ENGLISH (ASS) - SIGNS & SONGS"));
    // Already included, so it is not appended twice.
    QCOMPARE(subtitleDisplayLabel(QStringLiteral("Signs & Songs"),
                                  QStringLiteral("English - Signs & Songs"),
                                  QString()),
             QStringLiteral("ENGLISH - SIGNS & SONGS"));
    QCOMPARE(subtitleDisplayLabel(QString(),
                                  QStringLiteral("English (ASS)"),
                                  QStringLiteral("English (ASS) - Signs & Songs")),
             QStringLiteral("ENGLISH (ASS) - SIGNS & SONGS"));
    QCOMPARE(subtitleDisplayLabel(QString(), QString(), QString()),
             QStringLiteral("UNKNOWN"));

    // The preference matches a signs name that lives only on extendedDisplayTitle.
    QJsonObject signs{
        {QStringLiteral("id"), 12},
        {QStringLiteral("streamType"), 3},
        {QStringLiteral("codec"), QStringLiteral("ass")},
        {QStringLiteral("displayTitle"), QStringLiteral("English (ASS)")},
        {QStringLiteral("extendedDisplayTitle"), QStringLiteral("English (ASS) - Signs & Songs")},
        {QStringLiteral("selected"), false},
    };
    const QJsonObject item = itemWithMedia(QJsonArray{mediaWithStreams(QJsonArray{
        videoStream(1, "h264"),
        subtitleStream(3, "ass", "Full Dialogue", true),
        signs,
    })});
    QCOMPARE(planItemPlayback(item, prefsFor("never", "auto", true)).selectedSubtitleId,
             QStringLiteral("12"));
}

void PlexPlaybackTests::signsSongsBeatsThePlexSelection() {
    const QJsonArray streams{
        videoStream(1, "h264"),
        subtitleStream(3, "ass", "Full Dialogue", true),
        subtitleStream(4, "ass", "Songs", false),
        subtitleStream(5, "ass", "Signs & Songs", false),
    };
    const QJsonObject item = itemWithMedia(QJsonArray{mediaWithStreams(streams)});

    const PlaybackPlan preferred = planItemPlayback(item, prefsFor("never", "auto", true));
    QCOMPARE(preferred.selectedSubtitleId, QStringLiteral("5"));
    QCOMPARE(preferred.signsSongsSubtitleId, QStringLiteral("5"));
    QVERIFY(!preferred.selectedSubtitleImage);
    QVERIFY(!preferred.forceTranscode);
    QVERIFY(!preferred.burnSubtitles);

    // The signs track is reported even when the preference is off, so autoplay
    // can still find it. The selected track stays the one Plex marked.
    const PlaybackPlan plain = planItemPlayback(item, prefsFor("never", "auto", false));
    QCOMPARE(plain.selectedSubtitleId, QStringLiteral("3"));
    QCOMPARE(plain.signsSongsSubtitleId, QStringLiteral("5"));
}

void PlexPlaybackTests::signsSongsRanksBothWordsAboveOne() {
    const QJsonArray onlyOneWord{
        videoStream(1, "h264"),
        subtitleStream(6, "srt", "Songs", false),
        subtitleStream(7, "srt", "Signs", false),
    };
    const PlaybackPlan ranked = planItemPlayback(
        itemWithMedia(QJsonArray{mediaWithStreams(onlyOneWord)}),
        prefsFor("never", "auto", true));
    QCOMPARE(ranked.selectedSubtitleId, QStringLiteral("7"));

    // Same rank: the Plex-selected copy wins over the earlier one.
    const QJsonArray tie{
        videoStream(1, "h264"),
        subtitleStream(8, "ass", "Signs", false),
        subtitleStream(9, "ass", "Signs & Songs", true),
        subtitleStream(10, "ass", "Signs & Songs", false),
    };
    const PlaybackPlan tied = planItemPlayback(
        itemWithMedia(QJsonArray{mediaWithStreams(tie)}),
        prefsFor("av1", "auto", true));
    // Not AV1, so this also checks the preference doesn't force a transcode.
    QVERIFY(!tied.forceTranscode);
    QCOMPARE(tied.selectedSubtitleId, QStringLiteral("9"));

    // No signs track and nothing selected by Plex stays Off.
    const QJsonObject bare = itemWithMedia(QJsonArray{
        mediaWithStreams(QJsonArray{
            videoStream(1, "h264"),
            subtitleStream(11, "srt", "Full Dialogue", false),
        })
    });
    QCOMPARE(planItemPlayback(bare, prefsFor("never", "auto", true)).selectedSubtitleId,
             QStringLiteral("0"));
}

void PlexPlaybackTests::signsSongsOffKeepsThePlexSelection() {
    const QJsonObject item = itemWithMedia(QJsonArray{mediaWithStreams(QJsonArray{
        videoStream(1, "av1"),
        subtitleStream(3, "ass", "Full Dialogue", true),
        subtitleStream(5, "ass", "Signs & Songs", false),
    })});
    const PlaybackPlan plan = planItemPlayback(item, prefsFor("av1", "4000", false));
    QVERIFY(plan.forceTranscode);
    QCOMPARE(plan.selectedSubtitleId, QStringLiteral("3"));
    QCOMPARE(plan.bitrateKbps, QStringLiteral("4000"));
    QVERIFY(!plan.burnSubtitles);
}

void PlexPlaybackTests::textSubsAreNotBurnedImageSubsAre() {
    QVERIFY(!isImageSubtitleCodec(QStringLiteral("ass")));
    QVERIFY(!isImageSubtitleCodec(QStringLiteral("ssa")));
    QVERIFY(!isImageSubtitleCodec(QStringLiteral("srt")));
    QVERIFY(!isImageSubtitleCodec(QStringLiteral("subrip")));
    QVERIFY(isImageSubtitleCodec(QStringLiteral("pgssub")));
    QVERIFY(isImageSubtitleCodec(QStringLiteral("PGS")));
    QVERIFY(isImageSubtitleCodec(QStringLiteral("dvdsub")));
    QVERIFY(isImageSubtitleCodec(QStringLiteral("vobsub")));
    QVERIFY(isImageSubtitleCodec(QStringLiteral("hdmv_pgs_subtitle")));
    QVERIFY(shouldBurnSubtitle(QStringLiteral("5"), true));
    QVERIFY(!shouldBurnSubtitle(QStringLiteral("5"), false));
    QVERIFY(!shouldBurnSubtitle(QStringLiteral("0"), true));
    QVERIFY(!shouldBurnSubtitle(QString(), true));

    const QJsonObject text = itemWithMedia(QJsonArray{mediaWithStreams(QJsonArray{
        videoStream(1, "av1"),
        subtitleStream(5, "ass", "Signs & Songs", true),
    })});
    const PlaybackPlan textPlan = planItemPlayback(text, prefsFor("av1", "auto", true));
    QVERIFY(textPlan.forceTranscode);
    QCOMPARE(textPlan.selectedSubtitleId, QStringLiteral("5"));
    QVERIFY(!textPlan.burnSubtitles);
    QCOMPARE(textPlan.bitrateKbps, QStringLiteral("4000"));

    const QJsonObject image = itemWithMedia(QJsonArray{mediaWithStreams(QJsonArray{
        videoStream(1, "av1"),
        subtitleStream(6, "pgssub", "English", true),
    })});
    const PlaybackPlan imagePlan = planItemPlayback(image, prefsFor("all", "2000", true));
    QVERIFY(imagePlan.forceTranscode);
    QVERIFY(imagePlan.selectedSubtitleImage);
    QVERIFY(imagePlan.burnSubtitles);
    QCOMPARE(imagePlan.bitrateKbps, QStringLiteral("2000"));

    // A signs track that is itself PGS is still burned: there is no text sidecar.
    const QJsonObject imageSigns = itemWithMedia(QJsonArray{mediaWithStreams(QJsonArray{
        videoStream(1, "h264"),
        subtitleStream(3, "ass", "Full Dialogue", true),
        subtitleStream(6, "vobsub", "Signs", false),
    })});
    const PlaybackPlan burnedSigns = planItemPlayback(imageSigns, prefsFor("all", "8000", true));
    QCOMPARE(burnedSigns.selectedSubtitleId, QStringLiteral("6"));
    QVERIFY(burnedSigns.burnSubtitles);

    // Direct play never burns, even when the selected track is an image sub.
    const PlaybackPlan direct = planItemPlayback(imageSigns, prefsFor("never", "auto", true));
    QCOMPARE(direct.selectedSubtitleId, QStringLiteral("6"));
    QVERIFY(!direct.forceTranscode);
    QVERIFY(!direct.burnSubtitles);
}

void PlexPlaybackTests::secondMediaVersionIsIgnored() {
    // Media[0] is H.264. A later AV1 version must not flip AV1-only on, and
    // the reverse must not flip it off.
    const QJsonObject h264First = itemWithMedia(QJsonArray{
        mediaWithStreams(QJsonArray{videoStream(1, "h264")}, "h264"),
        mediaWithStreams(QJsonArray{videoStream(9, "av1")}, "av1"),
    });
    QVERIFY(!planItemPlayback(h264First, prefsFor("av1", "auto")).forceTranscode);

    const QJsonObject av1First = itemWithMedia(QJsonArray{
        mediaWithStreams(QJsonArray{videoStream(1, "av1")}, "av1"),
        mediaWithStreams(QJsonArray{videoStream(9, "h264")}, "h264"),
    });
    QVERIFY(planItemPlayback(av1First, prefsFor("av1", "auto")).forceTranscode);

    // The first video stream wins when a part carries more than one.
    const QJsonObject firstStream = itemWithMedia(QJsonArray{mediaWithStreams(QJsonArray{
        videoStream(1, "h264"),
        videoStream(2, "av1"),
    })});
    QCOMPARE(planItemPlayback(firstStream, prefsFor("av1", "auto")).videoCodec, QStringLiteral("h264"));
    QVERIFY(!planItemPlayback(firstStream, prefsFor("av1", "auto")).forceTranscode);
}

void PlexPlaybackTests::streamCodecBeatsMediaCodec() {
    const QJsonObject item = itemWithMedia(QJsonArray{
        mediaWithStreams(QJsonArray{videoStream(1, "h264")}, "av1")
    });
    const PlaybackPlan plan = planItemPlayback(item, prefsFor("av1", "auto"));
    QCOMPARE(plan.videoCodec, QStringLiteral("h264"));
    QVERIFY(!plan.forceTranscode);
}

void PlexPlaybackTests::missingStreamCodecUsesMediaCodec() {
    const QJsonObject item = itemWithMedia(QJsonArray{
        mediaWithStreams(QJsonArray{videoStream(1, "")}, "av01")
    });
    const PlaybackPlan plan = planItemPlayback(item, prefsFor("av1", "auto"));
    QCOMPARE(plan.videoCodec, QStringLiteral("av01"));
    QVERIFY(plan.forceTranscode);
}

QTEST_GUILESS_MAIN(PlexPlaybackTests)
#include "tst_plex_playback.moc"
