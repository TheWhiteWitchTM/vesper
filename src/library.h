#pragma once

#include "common.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>

#include <atomic>

class QTimer;

class Library : public QObject {
    Q_OBJECT
public:
    explicit Library(QObject *parent = nullptr);

    bool open();
    QString lastError() const { return error_; }

    QList<Folder> folders() const;
    qint64 addFolder(const QString &path, bool watchStartup, const QString &role = {});
    void setFolderRole(qint64 id, const QString &role);
    void removeFolder(qint64 id);
    // Rewrites show names, seasons, and the music shelf from folder names.
    QString reclassify();
    QString refreshNote() const { return refreshNote_; }

    bool upsertTracks(const QList<Track> &tracks);
    void removeMissing(qint64 folderId, const QSet<QString> &seen);
    QHash<QString, QPair<qint64, qint64>> knownFiles(qint64 folderId) const;

    QList<Track> tracks(const QString &kind) const;
    QList<Album> albums(int limit = 0) const;
    QList<ArtistRow> artists() const;
    QList<GenreRow> genres() const;
    QList<Track> albumTracks(const QString &albumArtist, const QString &album) const;
    QList<Track> artistTracks(const QString &artist) const;
    QList<Track> genreTracks(const QString &genre) const;
    QList<Track> search(const QString &text) const;
    QList<Track> recentlyPlayed(int limit) const;
    QList<Track> recentlyAdded(int limit) const;
    QList<Track> favorites(int limit = 0) const;
    QList<Track> mostPlayed(int limit) const;
    QList<Track> continueWatching(int limit) const;
    QList<Track> needsMetadata(int limit) const;

    Track trackById(qint64 id) const;
    Track trackByPath(const QString &path) const;
    QString albumArtwork(const QString &albumArtist, const QString &album) const;

    void updateProbe(qint64 id, qint64 durationMs, int width, int height);
    void updateArtwork(qint64 id, const QString &artwork);
    void setAlbumArtwork(const QString &albumArtist, const QString &album, const QString &artwork);
    void updateLyrics(qint64 id, const QString &lyrics);
    void updateOverview(qint64 id, const QString &overview);
    void updatePlayback(qint64 id, qint64 positionMs, bool countPlay);
    void setFavorite(qint64 id, bool on);
    void setRating(qint64 id, int rating);
    void updateEditable(const Track &track);
    void setContent(qint64 id, const QString &label);
    void setIdLookup(const QList<qint64> &ids, int state);
    void relocateTrack(const QString &from, const QString &to);
    void setMetaTried(qint64 id, bool tried);
    void resetMissingMetadata();

    qint64 createPlaylist(const QString &name);
    void renamePlaylist(qint64 id, const QString &name);
    void deletePlaylist(qint64 id);
    QList<QPair<qint64, QString>> playlists() const;
    QList<Track> playlistTracks(qint64 id) const;
    void addToPlaylist(qint64 playlistId, const QList<qint64> &trackIds);
    void removePlaylistItems(const QList<qint64> &itemIds);

    LibraryStats stats() const;
    // After a worker thread wrote the database itself.
    void refresh();

signals:
    void changed();
    void trackUpdated(const Track &track);
    void foldersChanged();

private:
    Track trackFromQuery(const QSqlQuery &query) const;
    QList<Track> queryTracks(const QString &sql, const QVariantList &binds = {}) const;
    void touch();
    void migrate();

    QString applyClassification();

    QSqlDatabase db_;
    QString error_;
    QString refreshNote_;
    QTimer *touchTimer_ = nullptr;
};

// Safe to call from a worker thread. Opens its own connection.
QList<Track> readTracksKind(const QString &databasePath, const QString &kind);

// Safe to call from the scanner thread on a connection that thread opened.
bool writeTrackBatch(QSqlDatabase &db, const QList<Track> &tracks, QString *error);
bool forgetMissingTracks(QSqlDatabase &db, qint64 folderId, const QStringList &seen,
                         const std::atomic_bool *cancel);
QHash<QString, QPair<qint64, qint64>> knownTrackFiles(const QSqlDatabase &db, qint64 folderId);
