#pragma once

#include "common.h"

// Read embedded tags and artwork. Safe to call off the UI thread.
Track readTrackTags(const QString &path, const QString &artworkDirectory);

// Write the editable tags. Artwork and lyrics are left as they are.
// Returns false when this container cannot be saved.
bool writeTrackTags(const Track &track, QString *error);
