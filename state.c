#include "common.h"

void ProbeMedia(MediaItem *item);
void UpdateVideoFrame(AppState *state);

// ---------------------------------------------------------------------------
// Save / Load
// ---------------------------------------------------------------------------

void SaveState(AppState *state) {
    if (state->count == 0 || strlen(state->workingDir) == 0) return;
    char path[MAX_PATH_LEN + 64];
    snprintf(path, sizeof(path), "%s/%s", state->workingDir, STATE_FILENAME);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    int ver = SAVE_VERSION;
    fwrite(&ver,              sizeof(int),       1,             f);
    fwrite(&state->count,     sizeof(int),       1,             f);
    fwrite(&state->currentIndex, sizeof(int),    1,             f);
    fwrite(state->items,      sizeof(MediaItem), state->count,  f);
    fclose(f);
}

bool LoadState(AppState *state) {
    char path[MAX_PATH_LEN + 64];
    snprintf(path, sizeof(path), "%s/%s", state->workingDir, STATE_FILENAME);
    if (!FileExists(path)) return false;

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    int ver = 0;
    fread(&ver, sizeof(int), 1, f);
    if (ver != SAVE_VERSION) { fclose(f); return false; } // version mismatch → fresh scan

    int count = 0;
    if (fread(&count, sizeof(int), 1, f) != 1 || count == 0) { fclose(f); return false; }

    int idx = 0;
    if (fread(&idx, sizeof(int), 1, f) != 1) idx = 0;

    state->count    = count;
    state->capacity = count + 16;
    state->items    = (MediaItem *)malloc(state->capacity * sizeof(MediaItem));
    if (!state->items) { fclose(f); return false; }

    if (fread(state->items, sizeof(MediaItem), count, f) != (size_t)count) {
        free(state->items);
        state->count = 0;
        fclose(f);
        return false;
    }

    state->currentIndex = (idx >= 0 && idx < count) ? idx : 0;
    fclose(f);
    return true;
}

// ---------------------------------------------------------------------------
// Add a single file to the list
// ---------------------------------------------------------------------------

void AddFile(AppState *state, const char *path) {
    MediaType type = MEDIA_TYPE_UNKNOWN;
    if      (IsVideo(path)) type = MEDIA_TYPE_VIDEO;
    else if (IsImage(path)) type = MEDIA_TYPE_IMAGE;
    if (type == MEDIA_TYPE_UNKNOWN) return;

    if (state->count >= state->capacity) {
        state->capacity = (state->capacity == 0) ? 8 : state->capacity * 2;
        state->items    = (MediaItem *)realloc(state->items, state->capacity * sizeof(MediaItem));
    }

    MediaItem *item = &state->items[state->count];
    memset(item, 0, sizeof(MediaItem)); // zero everything first

    snprintf(item->fullPath, MAX_PATH_LEN, "%s", path);
    snprintf(item->fileName, 256,          "%s", GetFileName(path));
    item->type      = type;
    item->rotation  = 0;
    item->cropRect  = (Rectangle){0, 0, 0, 0};
    item->trimStart = 0;
    item->trimEnd   = 0;
    item->fps       = 0;
    item->totalFrames = 0;
    // skip / touched / isProcessed / isDeleted / metaLoaded all 0 from memset

    state->count++;
}

// ---------------------------------------------------------------------------
// Load the currently-indexed item into the texture + reset view state
// ---------------------------------------------------------------------------

void LoadActiveMedia(AppState *state) {
    if (state->currentIndex < 0 || state->currentIndex >= state->count) return;

    if (state->textureLoaded) {
        UnloadTexture(state->currentTexture);
        state->textureLoaded = false;
    }

    MediaItem *item = &state->items[state->currentIndex];
    if (item->isDeleted) return;

    if (!item->metaLoaded || (item->duration <= 0 && item->type == MEDIA_TYPE_VIDEO)) ProbeMedia(item);

    int visW = (item->rotation % 180 == 0) ? item->width  : item->height;
    int visH = (item->rotation % 180 == 0) ? item->height : item->width;

    if (item->cropRect.width == 0) {
        item->cropRect = (Rectangle){0, 0, (float)visW, (float)visH};
    }
    if (item->trimEnd == 0 && item->duration > 0) {
        item->trimEnd = item->duration;
    }

    // Reset view
    state->zoom             = 1.0f;
    state->pan              = (Vector2){0, 0};
    state->currentVideoTime = item->trimStart;
    state->currentFrame     = (item->fps > 0)
                              ? (int)(item->trimStart * item->fps)
                              : 0;

    if (item->type == MEDIA_TYPE_IMAGE) {
        state->currentTexture = LoadTexture(item->fullPath);
        state->textureLoaded  = true;
    } else if (item->type == MEDIA_TYPE_VIDEO) {
        UpdateVideoFrame(state);
    }
}
