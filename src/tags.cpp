#include "tags.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>

#include <taglib/attachedpictureframe.h>
#include <taglib/fileref.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/id3v2tag.h>
#include <taglib/mp4file.h>
#include <taglib/mpegfile.h>
#include <taglib/synchronizedlyricsframe.h>
#include <taglib/tpropertymap.h>
#include <taglib/unsynchronizedlyricsframe.h>

namespace {

QString fromTag(const TagLib::String &s)
{
    if (s.isEmpty())
        return {};
    return QString::fromUtf8(s.toCString(true)).trimmed();
}

QString prop(const TagLib::PropertyMap &map, const char *key)
{
    const TagLib::String k(key);
    if (!map.contains(k) || map[k].isEmpty())
        return {};
    return fromTag(map[k].front());
}

int leadingInt(const QString &text)
{
    QString number;
    for (const QChar c : text) {
        if (!c.isDigit())
            break;
        number.append(c);
    }
    return number.toInt();
}

QString saveArt(const QByteArray &bytes, const QString &key, const QString &artworkDirectory)
{
    if (bytes.size() < 32 || artworkDirectory.isEmpty())
        return {};
    QImage image;
    if (!image.loadFromData(bytes))
        return {};
    QDir().mkpath(artworkDirectory);
    const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex();
    const QString path = artworkDirectory + QLatin1Char('/') + QString::fromLatin1(hash) + QStringLiteral(".jpg");
    if (!QFileInfo::exists(path)) {
        if (!image.save(path, "JPEG", 90))
            return {};
    }
    return path;
}

QByteArray artFromMpeg(const char *path)
{
    TagLib::MPEG::File file(path);
    if (!file.isValid() || !file.ID3v2Tag())
        return {};
    const auto frames = file.ID3v2Tag()->frameListMap()["APIC"];
    const TagLib::ID3v2::AttachedPictureFrame *fallback = nullptr;
    for (auto *frame : frames) {
        auto *pic = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame *>(frame);
        if (!pic || pic->picture().isEmpty())
            continue;
        if (pic->type() == TagLib::ID3v2::AttachedPictureFrame::FrontCover)
            return QByteArray(pic->picture().data(), int(pic->picture().size()));
        if (!fallback)
            fallback = pic;
    }
    if (!fallback)
        return {};
    return QByteArray(fallback->picture().data(), int(fallback->picture().size()));
}

QString lyricsFromMpeg(const char *path)
{
    TagLib::MPEG::File file(path);
    if (!file.isValid() || !file.ID3v2Tag())
        return {};
    TagLib::ID3v2::Tag *tag = file.ID3v2Tag();
    const auto synced = tag->frameListMap()["SYLT"];
    if (!synced.isEmpty()) {
        auto *frame = dynamic_cast<TagLib::ID3v2::SynchronizedLyricsFrame *>(synced.front());
        if (frame && frame->timestampFormat() == TagLib::ID3v2::SynchronizedLyricsFrame::AbsoluteMilliseconds) {
            QString lrc;
            for (const auto &line : frame->synchedText()) {
                const unsigned t = line.time;
                const int min = int(t / 60000);
                const int sec = int((t / 1000) % 60);
                const int cs = int((t % 1000) / 10);
                lrc += QStringLiteral("[%1:%2.%3]%4\n")
                           .arg(min, 2, 10, QChar('0'))
                           .arg(sec, 2, 10, QChar('0'))
                           .arg(cs, 2, 10, QChar('0'))
                           .arg(fromTag(line.text));
            }
            if (!lrc.trimmed().isEmpty())
                return lrc.trimmed();
        }
    }
    const auto plain = tag->frameListMap()["USLT"];
    if (!plain.isEmpty()) {
        auto *frame = dynamic_cast<TagLib::ID3v2::UnsynchronizedLyricsFrame *>(plain.front());
        if (frame)
            return fromTag(frame->text());
    }
    return {};
}

QByteArray artFromFlac(const char *path)
{
    TagLib::FLAC::File file(path);
    if (!file.isValid())
        return {};
    const auto pics = file.pictureList();
    const TagLib::FLAC::Picture *fallback = nullptr;
    for (const auto *pic : pics) {
        if (!pic || pic->data().isEmpty())
            continue;
        if (pic->type() == TagLib::FLAC::Picture::FrontCover)
            return QByteArray(pic->data().data(), int(pic->data().size()));
        if (!fallback)
            fallback = pic;
    }
    if (!fallback)
        return {};
    return QByteArray(fallback->data().data(), int(fallback->data().size()));
}

QByteArray artFromMp4(const char *path)
{
    TagLib::MP4::File file(path);
    if (!file.isValid() || !file.tag())
        return {};
    const TagLib::MP4::Item item = file.tag()->item("covr");
    if (!item.isValid())
        return {};
    const auto covers = item.toCoverArtList();
    if (covers.isEmpty() || covers.front().data().isEmpty())
        return {};
    const auto &data = covers.front().data();
    return QByteArray(data.data(), int(data.size()));
}

bool genericFolder(const QString &name)
{
    const QString n = name.toLower();
    return n == QLatin1String("music") || n == QLatin1String("audio") || n == QLatin1String("songs")
        || n == QLatin1String("videos") || n == QLatin1String("video") || n == QLatin1String("movies")
        || n == QLatin1String("downloads") || n == QLatin1String("desktop");
}

} // namespace

Track readTrackTags(const QString &path, const QString &artworkDirectory)
{
    Track track;
    const QFileInfo info(path);
    track.path = info.absoluteFilePath();
    track.fileSize = info.size();
    track.mtime = info.lastModified().toSecsSinceEpoch();
    const QString ext = info.suffix().toLower();
    track.kind = isVideoExt(ext) ? QStringLiteral("video") : QStringLiteral("audio");

    const QByteArray encoded = QFile::encodeName(track.path);
    const char *cpath = encoded.constData();
    TagLib::FileRef ref(cpath, true, TagLib::AudioProperties::Average);
    if (!ref.isNull()) {
        if (ref.tag()) {
            track.title = fromTag(ref.tag()->title());
            track.artist = fromTag(ref.tag()->artist());
            track.album = fromTag(ref.tag()->album());
            track.genre = fromTag(ref.tag()->genre());
            track.year = int(ref.tag()->year());
            track.trackNo = int(ref.tag()->track());
        }
        if (ref.audioProperties()) {
            track.durationMs = ref.audioProperties()->lengthInMilliseconds();
            track.bitrate = ref.audioProperties()->bitrate();
        }
        if (ref.file()) {
            const TagLib::PropertyMap map = ref.file()->properties();
            track.albumArtist = prop(map, "ALBUMARTIST");
            if (track.albumArtist.isEmpty())
                track.albumArtist = prop(map, "ALBUM ARTIST");
            const QString disc = prop(map, "DISCNUMBER");
            if (!disc.isEmpty())
                track.discNo = leadingInt(disc);
            if (track.year == 0) {
                const QString date = prop(map, "DATE");
                if (date.size() >= 4)
                    track.year = date.left(4).toInt();
            }
            if (track.lyrics.isEmpty())
                track.lyrics = prop(map, "LYRICS");
            if (track.lyrics.isEmpty())
                track.lyrics = prop(map, "UNSYNCEDLYRICS");
        }
    }

    if (nameIsCrypticId(track.title)
        || (nameIsCrypticId(info.completeBaseName())
            && (track.title == info.completeBaseName() || track.title == cleanMediaTitle(info.completeBaseName()))))
        track.title.clear();
    if (track.title.isEmpty() && !nameIsCrypticId(info.completeBaseName()))
        track.title = cleanMediaTitle(info.completeBaseName());
    if (track.kind == QLatin1String("audio") && track.album.isEmpty()) {
        const QString parent = info.dir().dirName();
        if (!genericFolder(parent))
            track.album = parent;
    }

    QByteArray art;
    if (ext == QLatin1String("mp3")) {
        art = artFromMpeg(cpath);
        if (track.lyrics.isEmpty())
            track.lyrics = lyricsFromMpeg(cpath);
    } else if (ext == QLatin1String("flac")) {
        art = artFromFlac(cpath);
    } else if (ext == QLatin1String("m4a") || ext == QLatin1String("mp4") || ext == QLatin1String("m4v")) {
        art = artFromMp4(cpath);
    }
    if (!art.isEmpty()) {
        const QString key = track.kind == QLatin1String("audio")
            ? QStringLiteral("album|%1|%2").arg(track.albumKeyArtist().toLower(), track.displayAlbum().toLower())
            : QStringLiteral("video|%1").arg(track.path);
        track.artwork = saveArt(art, key, artworkDirectory);
    }
    return track;
}
