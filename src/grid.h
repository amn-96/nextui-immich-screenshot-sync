#ifndef GRID_H
#define GRID_H

// Thumbnail grid over the newest-first assets of album_ids, with a viewer. Returns when the user presses B.
// album_ids must stay valid until it returns.
// Downloaded originals go to Screenshots when every album is a "_NextUI" album in the cached album list,
// otherwise to Pictures/Immich.
// device: also list local screenshots not yet in the album, and badge server assets as uploaded.
void grid_run(const char* title, const char** album_ids, int album_count, int device);

#endif
