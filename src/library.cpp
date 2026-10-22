#include "library.h"

#include <QDateTime>
#include <QDir>
#include <QSqlError>
#include <QSqlRecord>
#include <QTimer>
#include <QVariant>

Library::Library(QObject *parent)
    : QObject(parent)
{
}

bool Library::open()
{
    QDir().mkpath(dataDir());
    QDir().mkpath(artworkDir());
    if (QSqlDatabase::contains(QStringLiteral("vesper")))
        db_ = QSqlDatabase::database(QStringLiteral("vesper"));
    else
        db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("vesper"));
    db_.setDatabaseName(dataDir() + QStringLiteral("/library.db"));
    if (!db_.open()) {
        error_ = db_.lastError().text();
        return false;
    }
    QSqlQuery q(db_);
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    q.exec(QStringLiteral("PRAGMA busy_timeout=4000"));
    migrate();
    return true;
}

void Library::migrate()
{
    QSqlQuery q(db_);
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS folders ("
        " id INTEGER PRIMARY KEY,"
        " path TEXT UNIQUE NOT NULL,"
        " watch_startup INTEGER NOT NULL DEFAULT 1)"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS tracks ("
        " id INTEGER PRIMARY KEY,"
        " folder_id INTEGER,"
        " path TEXT UNIQUE NOT NULL,"
        " kind TEXT NOT NULL,"
        " title TEXT,"
        " artist TEXT,"
        " album TEXT,"
        " album_artist TEXT,"
        " genre TEXT,"
        " year INTEGER,"
        " track_no INTEGER,"
        " disc_no INTEGER,"
        " duration_ms INTEGER,"
        " bitrate INTEGER,"
        " file_size INTEGER,"
        " mtime INTEGER,"
        " artwork TEXT,"
        " favorite INTEGER DEFAULT 0,"
        " rating INTEGER DEFAULT 0,"
        " play_count INTEGER DEFAULT 0,"
        " last_played INTEGER,"
        " position_ms INTEGER DEFAULT 0,"
        " lyrics TEXT,"
        " overview TEXT,"
        " width INTEGER,"
        " height INTEGER,"
        " date_added INTEGER,"
        " meta_tried INTEGER DEFAULT 0)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_tracks_kind ON tracks(kind)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_tracks_album ON tracks(album_artist, album)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_tracks_artist ON tracks(artist)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_tracks_folder ON tracks(folder_id)"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS playlists ("
        " id INTEGER PRIMARY KEY,"
        " name TEXT NOT NULL,"
        " created INTEGER NOT NULL)"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS playlist_items ("
        " id INTEGER PRIMARY KEY,"
        " playlist_id INTEGER NOT NULL,"
        " track_id INTEGER NOT NULL,"
        " position INTEGER NOT NULL)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_pi ON playlist_items(playlist_id, position)"));
}

void Library::touch()
{
    if (!touchTimer_) {
        touchTimer_ = new QTimer(this);
        touchTimer_->setSingleShot(true);
        touchTimer_->setInterval(180);
        connect(touchTimer_, &QTimer::timeout, this, &Library::changed);
    }
    touchTimer_->start();
}

QList<Folder> Library::folders() const
{
    QList<Folder> out;
    QSqlQuery q(db_);
    q.exec(QStringLiteral("SELECT id, path, watch_startup FROM folders ORDER BY path COLLATE NOCASE"));
    while (q.next()) {
        Folder f;
        f.id = q.value(0).toLongLong();
        f.path = q.value(1).toString();
        f.watchStartup = q.value(2).toInt() != 0;
        out.append(f);
    }
    return out;
}

qint64 Library::addFolder(const QString &path, bool watchStartup)
{
    const QString clean = cleanDirPath(path);
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "INSERT INTO folders(path, watch_startup) VALUES(?, ?) "
        "ON CONFLICT(path) DO UPDATE SET watch_startup=excluded.watch_startup"));
    q.addBindValue(clean);
    q.addBindValue(watchStartup ? 1 : 0);
    if (!q.exec()) {
        error_ = q.lastError().text();
        return 0;
    }
    QSqlQuery idq(db_);
    idq.prepare(QStringLiteral("SELECT id FROM folders WHERE path=?"));
    idq.addBindValue(clean);
    idq.exec();
    qint64 id = 0;
    if (idq.next())
        id = idq.value(0).toLongLong();
    emit foldersChanged();
    return id;
}

void Library::removeFolder(qint64 id)
{
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("DELETE FROM tracks WHERE folder_id=?"));
    q.addBindValue(id);
    q.exec();
    q.prepare(QStringLiteral("DELETE FROM folders WHERE id=?"));
    q.addBindValue(id);
    q.exec();
    emit foldersChanged();
    touch();
}

bool Library::upsertTracks(const QList<Track> &tracks)
{
    if (tracks.isEmpty())
        return true;
    if (!db_.transaction())
        return false;
    QSqlQuery q(db_);
    const QString sql = QStringLiteral(
        "INSERT INTO tracks ("
        " folder_id, path, kind, title, artist, album, album_artist, genre,"
        " year, track_no, disc_no, duration_ms, bitrate, file_size, mtime,"
        " artwork, lyrics, width, height, date_added) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(path) DO UPDATE SET "
        " folder_id=COALESCE(excluded.folder_id, tracks.folder_id),"
        " kind=excluded.kind,"
        " title=excluded.title,"
        " artist=excluded.artist,"
        " album=excluded.album,"
        " album_artist=excluded.album_artist,"
        " genre=excluded.genre,"
        " year=excluded.year,"
        " track_no=excluded.track_no,"
        " disc_no=excluded.disc_no,"
        " duration_ms=CASE WHEN excluded.duration_ms>0 THEN excluded.duration_ms ELSE tracks.duration_ms END,"
        " bitrate=CASE WHEN excluded.bitrate>0 THEN excluded.bitrate ELSE tracks.bitrate END,"
        " file_size=excluded.file_size,"
        " mtime=excluded.mtime,"
        " artwork=CASE WHEN excluded.artwork<>'' THEN excluded.artwork ELSE tracks.artwork END,"
        " lyrics=CASE WHEN tracks.lyrics IS NULL OR tracks.lyrics='' THEN excluded.lyrics ELSE tracks.lyrics END,"
        " width=CASE WHEN excluded.width>0 THEN excluded.width ELSE tracks.width END,"
        " height=CASE WHEN excluded.height>0 THEN excluded.height ELSE tracks.height END");
    if (!q.prepare(sql)) {
        error_ = q.lastError().text();
        db_.rollback();
        return false;
    }
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (const Track &t : tracks) {
        q.bindValue(0, t.folderId > 0 ? QVariant(t.folderId) : QVariant());
        q.bindValue(1, t.path);
        q.bindValue(2, t.kind);
        q.bindValue(3, t.title);
        q.bindValue(4, t.artist);
        q.bindValue(5, t.album);
        q.bindValue(6, t.albumArtist);
        q.bindValue(7, t.genre);
        q.bindValue(8, t.year);
        q.bindValue(9, t.trackNo);
        q.bindValue(10, t.discNo);
        q.bindValue(11, t.durationMs);
        q.bindValue(12, t.bitrate);
        q.bindValue(13, t.fileSize);
        q.bindValue(14, t.mtime);
        q.bindValue(15, t.artwork);
        q.bindValue(16, t.lyrics);
        q.bindValue(17, t.width);
        q.bindValue(18, t.height);
        q.bindValue(19, t.dateAdded > 0 ? t.dateAdded : now);
        if (!q.exec()) {
            error_ = q.lastError().text();
            db_.rollback();
            return false;
        }
    }
    if (!db_.commit()) {
        error_ = db_.lastError().text();
        return false;
    }
    touch();
    return true;
}

void Library::removeMissing(qint64 folderId, const QSet<QString> &seen)
{
    QSqlQuery q(db_);
    q.exec(QStringLiteral("CREATE TEMP TABLE IF NOT EXISTS seen_paths(path TEXT PRIMARY KEY)"));
    q.exec(QStringLiteral("DELETE FROM seen_paths"));
    db_.transaction();
    QSqlQuery ins(db_);
    ins.prepare(QStringLiteral("INSERT OR IGNORE INTO seen_paths(path) VALUES (?)"));
    for (const QString &path : seen) {
        ins.bindValue(0, path);
        ins.exec();
    }
    db_.commit();
    QSqlQuery del(db_);
    del.prepare(QStringLiteral(
        "DELETE FROM tracks WHERE folder_id=? AND path NOT IN (SELECT path FROM seen_paths)"));
    del.addBindValue(folderId);
    del.exec();
    touch();
}

QHash<QString, QPair<qint64, qint64>> Library::knownFiles(qint64 folderId) const
{
    QHash<QString, QPair<qint64, qint64>> out;
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("SELECT path, mtime, file_size FROM tracks WHERE folder_id=?"));
    q.addBindValue(folderId);
    q.exec();
    while (q.next())
        out.insert(q.value(0).toString(), {q.value(1).toLongLong(), q.value(2).toLongLong()});
    return out;
}

Track Library::trackFromQuery(const QSqlQuery &q) const
{
    Track t;
    t.id = q.value(QStringLiteral("id")).toLongLong();
    t.folderId = q.value(QStringLiteral("folder_id")).toLongLong();
    t.path = q.value(QStringLiteral("path")).toString();
    t.kind = q.value(QStringLiteral("kind")).toString();
    t.title = q.value(QStringLiteral("title")).toString();
    t.artist = q.value(QStringLiteral("artist")).toString();
    t.album = q.value(QStringLiteral("album")).toString();
    t.albumArtist = q.value(QStringLiteral("album_artist")).toString();
    t.genre = q.value(QStringLiteral("genre")).toString();
    t.year = q.value(QStringLiteral("year")).toInt();
    t.trackNo = q.value(QStringLiteral("track_no")).toInt();
    t.discNo = q.value(QStringLiteral("disc_no")).toInt();
    t.durationMs = q.value(QStringLiteral("duration_ms")).toLongLong();
    t.bitrate = q.value(QStringLiteral("bitrate")).toInt();
    t.fileSize = q.value(QStringLiteral("file_size")).toLongLong();
    t.mtime = q.value(QStringLiteral("mtime")).toLongLong();
    t.artwork = q.value(QStringLiteral("artwork")).toString();
    t.favorite = q.value(QStringLiteral("favorite")).toInt() != 0;
    t.rating = q.value(QStringLiteral("rating")).toInt();
    t.playCount = q.value(QStringLiteral("play_count")).toInt();
    t.lastPlayed = q.value(QStringLiteral("last_played")).toLongLong();
    t.positionMs = q.value(QStringLiteral("position_ms")).toLongLong();
    t.lyrics = q.value(QStringLiteral("lyrics")).toString();
    t.overview = q.value(QStringLiteral("overview")).toString();
    t.width = q.value(QStringLiteral("width")).toInt();
    t.height = q.value(QStringLiteral("height")).toInt();
    t.dateAdded = q.value(QStringLiteral("date_added")).toLongLong();
    t.metaTried = q.value(QStringLiteral("meta_tried")).toInt() != 0;
    if (q.record().contains(QStringLiteral("item_id")))
        t.playlistItemId = q.value(QStringLiteral("item_id")).toLongLong();
    return t;
}

QList<Track> Library::queryTracks(const QString &sql, const QVariantList &binds) const
{
    QList<Track> out;
    QSqlQuery q(db_);
    q.prepare(sql);
    for (const QVariant &b : binds)
        q.addBindValue(b);
    if (!q.exec())
        return out;
    while (q.next())
        out.append(trackFromQuery(q));
    return out;
}

QList<Track> Library::tracks(const QString &kind) const
{
    if (kind.isEmpty()) {
        return queryTracks(QStringLiteral(
            "SELECT * FROM tracks ORDER BY kind, artist COLLATE NOCASE, album COLLATE NOCASE, disc_no, track_no, title COLLATE NOCASE"));
    }
    return queryTracks(QStringLiteral(
        "SELECT * FROM tracks WHERE kind=? "
        "ORDER BY artist COLLATE NOCASE, album COLLATE NOCASE, disc_no, track_no, title COLLATE NOCASE"),
        {kind});
}

QList<Album> Library::albums() const
{
    QList<Album> out;
    QSqlQuery q(db_);
    q.exec(QStringLiteral(
        "SELECT COALESCE(NULLIF(album_artist,''), NULLIF(artist,''), 'Unknown artist'),"
        " COALESCE(NULLIF(album,''), 'Unknown album'),"
        " MAX(year), COUNT(*), SUM(duration_ms), MAX(NULLIF(artwork,'')) "
        "FROM tracks WHERE kind='audio' "
        "GROUP BY 1, 2 "
        "ORDER BY 1 COLLATE NOCASE, 3, 2 COLLATE NOCASE"));
    while (q.next()) {
        Album a;
        a.albumArtist = q.value(0).toString();
        a.album = q.value(1).toString();
        a.year = q.value(2).toInt();
        a.tracks = q.value(3).toInt();
        a.durationMs = q.value(4).toLongLong();
        a.artwork = q.value(5).toString();
        out.append(a);
    }
    return out;
}

QList<ArtistRow> Library::artists() const
{
    QList<ArtistRow> out;
    QSqlQuery q(db_);
    q.exec(QStringLiteral(
        "SELECT COALESCE(NULLIF(album_artist,''), NULLIF(artist,''), 'Unknown artist'),"
        " COUNT(DISTINCT COALESCE(NULLIF(album,''), 'Unknown album')), COUNT(*) "
        "FROM tracks WHERE kind='audio' GROUP BY 1 ORDER BY 1 COLLATE NOCASE"));
    while (q.next()) {
        ArtistRow a;
        a.name = q.value(0).toString();
        a.albums = q.value(1).toInt();
        a.tracks = q.value(2).toInt();
        out.append(a);
    }
    return out;
}

QList<GenreRow> Library::genres() const
{
    QList<GenreRow> out;
    QSqlQuery q(db_);
    q.exec(QStringLiteral(
        "SELECT COALESCE(NULLIF(genre,''), 'Unknown'), COUNT(*) "
        "FROM tracks WHERE kind='audio' GROUP BY 1 ORDER BY 1 COLLATE NOCASE"));
    while (q.next()) {
        GenreRow g;
        g.name = q.value(0).toString();
        g.tracks = q.value(1).toInt();
        out.append(g);
    }
    return out;
}

QList<Track> Library::albumTracks(const QString &albumArtist, const QString &album) const
{
    return queryTracks(QStringLiteral(
        "SELECT * FROM tracks WHERE kind='audio' "
        "AND COALESCE(NULLIF(album_artist,''), NULLIF(artist,''), 'Unknown artist')=? "
        "AND COALESCE(NULLIF(album,''), 'Unknown album')=? "
        "ORDER BY disc_no, track_no, title COLLATE NOCASE"),
        {albumArtist, album});
}

QList<Track> Library::artistTracks(const QString &artist) const
{
    return queryTracks(QStringLiteral(
        "SELECT * FROM tracks WHERE kind='audio' AND ("
        " COALESCE(NULLIF(album_artist,''), NULLIF(artist,''), 'Unknown artist')=? "
        " OR artist=?) "
        "ORDER BY album COLLATE NOCASE, disc_no, track_no"),
        {artist, artist});
}

QList<Track> Library::genreTracks(const QString &genre) const
{
    if (genre == QLatin1String("Unknown")) {
        return queryTracks(QStringLiteral(
            "SELECT * FROM tracks WHERE kind='audio' AND (genre IS NULL OR genre='') "
            "ORDER BY artist, album, disc_no, track_no"));
    }
    return queryTracks(QStringLiteral(
        "SELECT * FROM tracks WHERE kind='audio' AND genre=? "
        "ORDER BY artist, album, disc_no, track_no"),
        {genre});
}

QList<Track> Library::search(const QString &text) const
{
    const QString like = likePattern(text.trimmed());
    return queryTracks(QStringLiteral(
        "SELECT * FROM tracks WHERE title LIKE ? ESCAPE '\\' "
        "OR artist LIKE ? ESCAPE '\\' OR album LIKE ? ESCAPE '\\' "
        "OR album_artist LIKE ? ESCAPE '\\' OR genre LIKE ? ESCAPE '\\' "
        "ORDER BY CASE kind WHEN 'audio' THEN 0 ELSE 1 END, artist, album, track_no LIMIT 400"),
        {like, like, like, like, like});
}

QList<Track> Library::recentlyPlayed(int limit) const
{
    return queryTracks(QStringLiteral(
        "SELECT * FROM tracks WHERE last_played>0 ORDER BY last_played DESC LIMIT ?"),
        {limit});
}

QList<Track> Library::recentlyAdded(int limit) const
{
    return queryTracks(QStringLiteral(
        "SELECT * FROM tracks ORDER BY date_added DESC LIMIT ?"), {limit});
}

QList<Track> Library::favorites() const
{
    return queryTracks(QStringLiteral(
        "SELECT * FROM tracks WHERE favorite=1 "
        "ORDER BY artist COLLATE NOCASE, album, track_no"));
}

QList<Track> Library::mostPlayed(int limit) const
{
    return queryTracks(QStringLiteral(
        "SELECT * FROM tracks WHERE play_count>0 "
        "ORDER BY play_count DESC, last_played DESC LIMIT ?"),
        {limit});
}

QList<Track> Library::continueWatching(int limit) const
{
    return queryTracks(QStringLiteral(
        "SELECT * FROM tracks WHERE kind='video' AND position_ms>10000 "
        "AND (duration_ms=0 OR position_ms < duration_ms-15000) "
        "ORDER BY last_played DESC LIMIT ?"),
        {limit});
}

QList<Track> Library::needsMetadata(int limit) const
{
    return queryTracks(QStringLiteral(
        "SELECT * FROM tracks WHERE meta_tried=0 AND ("
        " artwork IS NULL OR artwork='' OR "
        " (kind='audio' AND (lyrics IS NULL OR lyrics='')) OR "
        " (kind='video' AND (overview IS NULL OR overview=''))) "
        "ORDER BY date_added DESC LIMIT ?"),
        {limit});
}

Track Library::trackById(qint64 id) const
{
    const QList<Track> rows = queryTracks(QStringLiteral("SELECT * FROM tracks WHERE id=?"), {id});
    return rows.isEmpty() ? Track{} : rows.first();
}

Track Library::trackByPath(const QString &path) const
{
    const QList<Track> rows = queryTracks(QStringLiteral("SELECT * FROM tracks WHERE path=?"), {path});
    return rows.isEmpty() ? Track{} : rows.first();
}

QString Library::albumArtwork(const QString &albumArtist, const QString &album) const
{
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "SELECT artwork FROM tracks WHERE artwork IS NOT NULL AND artwork<>'' "
        "AND COALESCE(NULLIF(album_artist,''), NULLIF(artist,''), 'Unknown artist')=? "
        "AND COALESCE(NULLIF(album,''), 'Unknown album')=? LIMIT 1"));
    q.addBindValue(albumArtist);
    q.addBindValue(album);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return {};
}

void Library::updateProbe(qint64 id, qint64 durationMs, int width, int height)
{
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "UPDATE tracks SET "
        "duration_ms=CASE WHEN ?>0 THEN ? ELSE duration_ms END, "
        "width=CASE WHEN ?>0 THEN ? ELSE width END, "
        "height=CASE WHEN ?>0 THEN ? ELSE height END WHERE id=?"));
    q.addBindValue(durationMs);
    q.addBindValue(durationMs);
    q.addBindValue(width);
    q.addBindValue(width);
    q.addBindValue(height);
    q.addBindValue(height);
    q.addBindValue(id);
    q.exec();
    emit trackUpdated(trackById(id));
}

void Library::updateArtwork(qint64 id, const QString &artwork)
{
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("UPDATE tracks SET artwork=? WHERE id=?"));
    q.addBindValue(artwork);
    q.addBindValue(id);
    q.exec();
    emit trackUpdated(trackById(id));
    touch();
}

void Library::setAlbumArtwork(const QString &albumArtist, const QString &album, const QString &artwork)
{
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "UPDATE tracks SET artwork=? WHERE (artwork IS NULL OR artwork='') AND kind='audio' "
        "AND COALESCE(NULLIF(album_artist,''), NULLIF(artist,''), 'Unknown artist')=? "
        "AND COALESCE(NULLIF(album,''), 'Unknown album')=?"));
    q.addBindValue(artwork);
    q.addBindValue(albumArtist);
    q.addBindValue(album);
    q.exec();
    touch();
}

void Library::updateLyrics(qint64 id, const QString &lyrics)
{
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("UPDATE tracks SET lyrics=? WHERE id=?"));
    q.addBindValue(lyrics);
    q.addBindValue(id);
    q.exec();
    emit trackUpdated(trackById(id));
}

void Library::updateOverview(qint64 id, const QString &overview)
{
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("UPDATE tracks SET overview=? WHERE id=?"));
    q.addBindValue(overview);
    q.addBindValue(id);
    q.exec();
    emit trackUpdated(trackById(id));
}

void Library::updatePlayback(qint64 id, qint64 positionMs, bool countPlay)
{
    if (id <= 0)
        return;
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "UPDATE tracks SET position_ms=?, last_played=?, play_count=play_count+? WHERE id=?"));
    q.addBindValue(positionMs);
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.addBindValue(countPlay ? 1 : 0);
    q.addBindValue(id);
    q.exec();
}

void Library::setFavorite(qint64 id, bool on)
{
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("UPDATE tracks SET favorite=? WHERE id=?"));
    q.addBindValue(on ? 1 : 0);
    q.addBindValue(id);
    q.exec();
    emit trackUpdated(trackById(id));
    touch();
}

void Library::setRating(qint64 id, int rating)
{
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("UPDATE tracks SET rating=? WHERE id=?"));
    q.addBindValue(qBound(0, rating, 5));
    q.addBindValue(id);
    q.exec();
    emit trackUpdated(trackById(id));
}

void Library::relocateTrack(const QString &from, const QString &to)
{
    if (from.isEmpty() || to.isEmpty() || from == to)
        return;
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("UPDATE tracks SET path=? WHERE path=?"));
    q.addBindValue(to);
    q.addBindValue(from);
    if (!q.exec() || q.numRowsAffected() <= 0)
        return;
    emit trackUpdated(trackByPath(to));
    touch();
}

void Library::updateEditable(const Track &track)
{
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "UPDATE tracks SET title=?, artist=?, album=?, album_artist=?, genre=?, year=?, track_no=? WHERE id=?"));
    q.addBindValue(track.title);
    q.addBindValue(track.artist);
    q.addBindValue(track.album);
    q.addBindValue(track.albumArtist);
    q.addBindValue(track.genre);
    q.addBindValue(track.year);
    q.addBindValue(track.trackNo);
    q.addBindValue(track.id);
    q.exec();
    emit trackUpdated(trackById(track.id));
    touch();
}

void Library::setMetaTried(qint64 id, bool tried)
{
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("UPDATE tracks SET meta_tried=? WHERE id=?"));
    q.addBindValue(tried ? 1 : 0);
    q.addBindValue(id);
    q.exec();
}

void Library::resetMissingMetadata()
{
    QSqlQuery q(db_);
    q.exec(QStringLiteral(
        "UPDATE tracks SET meta_tried=0 WHERE artwork IS NULL OR artwork='' "
        "OR (kind='audio' AND (lyrics IS NULL OR lyrics='')) "
        "OR (kind='video' AND (overview IS NULL OR overview=''))"));
}

qint64 Library::createPlaylist(const QString &name)
{
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("INSERT INTO playlists(name, created) VALUES(?, ?)"));
    q.addBindValue(name.trimmed().isEmpty() ? QStringLiteral("Playlist") : name.trimmed());
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    if (!q.exec())
        return 0;
    touch();
    return q.lastInsertId().toLongLong();
}

void Library::renamePlaylist(qint64 id, const QString &name)
{
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("UPDATE playlists SET name=? WHERE id=?"));
    q.addBindValue(name.trimmed());
    q.addBindValue(id);
    q.exec();
    touch();
}

void Library::deletePlaylist(qint64 id)
{
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("DELETE FROM playlist_items WHERE playlist_id=?"));
    q.addBindValue(id);
    q.exec();
    q.prepare(QStringLiteral("DELETE FROM playlists WHERE id=?"));
    q.addBindValue(id);
    q.exec();
    touch();
}

QList<QPair<qint64, QString>> Library::playlists() const
{
    QList<QPair<qint64, QString>> out;
    QSqlQuery q(db_);
    q.exec(QStringLiteral("SELECT id, name FROM playlists ORDER BY name COLLATE NOCASE"));
    while (q.next())
        out.append({q.value(0).toLongLong(), q.value(1).toString()});
    return out;
}

QList<Track> Library::playlistTracks(qint64 id) const
{
    return queryTracks(QStringLiteral(
        "SELECT tracks.*, playlist_items.id AS item_id FROM playlist_items "
        "JOIN tracks ON tracks.id = playlist_items.track_id "
        "WHERE playlist_items.playlist_id=? ORDER BY playlist_items.position, playlist_items.id"));
}

void Library::addToPlaylist(qint64 playlistId, const QList<qint64> &trackIds)
{
    QSqlQuery maxq(db_);
    maxq.prepare(QStringLiteral("SELECT COALESCE(MAX(position), -1) FROM playlist_items WHERE playlist_id=?"));
    maxq.addBindValue(playlistId);
    maxq.exec();
    int pos = 0;
    if (maxq.next())
        pos = maxq.value(0).toInt() + 1;
    QSqlQuery q(db_);
    q.prepare(QStringLiteral(
        "INSERT INTO playlist_items(playlist_id, track_id, position) VALUES(?,?,?)"));
    for (qint64 id : trackIds) {
        if (id <= 0)
            continue;
        q.bindValue(0, playlistId);
        q.bindValue(1, id);
        q.bindValue(2, pos++);
        q.exec();
    }
    touch();
}

void Library::removePlaylistItems(const QList<qint64> &itemIds)
{
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("DELETE FROM playlist_items WHERE id=?"));
    for (qint64 id : itemIds) {
        q.bindValue(0, id);
        q.exec();
    }
    touch();
}

LibraryStats Library::stats() const
{
    LibraryStats s;
    QSqlQuery q(db_);
    q.exec(QStringLiteral(
        "SELECT kind, COUNT(*), COALESCE(SUM(duration_ms),0) FROM tracks GROUP BY kind"));
    while (q.next()) {
        if (q.value(0).toString() == QLatin1String("audio"))
            s.audio = q.value(1).toInt();
        else if (q.value(0).toString() == QLatin1String("video"))
            s.video = q.value(1).toInt();
        s.durationMs += q.value(2).toLongLong();
    }
    return s;
}
