#pragma once

#include "common.h"

// Read embedded tags and artwork. Safe to call off the UI thread.
Track readTrackTags(const QString &path, const QString &artworkDirectory);
