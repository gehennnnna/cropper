#define _POSIX_C_SOURCE 200809L
#include "common.h"
#include <math.h>

void AddLog(AppState *state, const char *msg, Color col) {
    if (state->logs.count < MAX_LOGS) {
        strncpy(state->logs.messages[state->logs.count], msg, 256);
        state->logs.colors[state->logs.count] = col;
        state->logs.count++;
        if (state->logs.count > 10) state->logs.scrollOffset = state->logs.count - 10;
    }
}

void EnsureOutputDir(const char *workDir) {
    char path[MAX_PATH_LEN + 256];
    snprintf(path, sizeof(path), "%s/%s", workDir, OUTPUT_DIR_NAME);
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        #ifdef _WIN32
        mkdir(path);
        #else
        mkdir(path, 0700);
        #endif
    }
}

void ProbeMedia(MediaItem *item) {
    if (item->type == MEDIA_TYPE_VIDEO) {
        char cmd[MAX_PATH_LEN + 256];
        snprintf(cmd, sizeof(cmd), 
            "ffprobe -v error -select_streams v:0 -show_entries stream=width,height,duration,r_frame_rate -of csv=p=0 \"%s\"", 
            item->fullPath);
            
        FILE *fp = popen(cmd, "r");
        if (fp) {
            char buf[256] = {0};
            if (fgets(buf, sizeof(buf), fp)) {
                char *token = strtok(buf, ",");         // width
                if(token) item->width = atoi(token);
                token = strtok(NULL, ",");               // height
                if(token) item->height = atoi(token);
                token = strtok(NULL, ",");               // r_frame_rate (e.g. "30/1")
                if(token){
                    char *slash = strchr(token, '/');
                    if(slash){
                        float fN = atof(token);
                        float fD = atof(slash+1);
                        item->fps = (fD > 0) ? fN / fD : 30.f;
                    } else {
                        item->fps = atof(token);
                    }
                    if(item->fps <= 0) item->fps = 30.f;
                }
                token = strtok(NULL, ",\n");             // duration (e.g. "88.500000")
                if(token) item->duration = (float)atof(token);
            }
            pclose(fp);
        }
        if (item->fps <= 0) item->fps = 30.f;
        item->totalFrames = (int)(item->duration * item->fps);
        if (item->trimEnd == 0) item->trimEnd = item->duration;
    } else {
        Image img = LoadImage(item->fullPath);
        item->width = img.width; item->height = img.height;
        UnloadImage(img);
    }
    item->metaLoaded = true;
}


static void on_mpv_update(void *ctx) {
    // Required callback for mpv_render_context, but we poll manually per-frame.
}

void WorkerInit(FrameWorker *w) {
    w->mpv = mpv_create();
    if (!w->mpv) {
        fprintf(stderr, "Fatal error: failed to create mpv instance\n");
        return;
    }

    mpv_set_option_string(w->mpv, "vo", "libmpv");
    mpv_set_option_string(w->mpv, "audio", "no");    // Disable audio pipeline to prevent deadlock
    mpv_set_option_string(w->mpv, "hwdec", "no");    // Software decode handles random exact seeks instantly
    mpv_set_option_string(w->mpv, "hr-seek", "yes"); // Force accurate frame-by-frame seeking
    mpv_set_option_string(w->mpv, "keep-open", "yes"); // PREVENTS 10-second EOF CRASH!

    mpv_initialize(w->mpv);

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, MPV_RENDER_API_TYPE_SW},
        {MPV_RENDER_PARAM_INVALID, NULL}
    };
    
    if (mpv_render_context_create(&w->mpv_ctx, w->mpv, params) < 0) {
        fprintf(stderr, "Fatal error: failed to initialize mpv SW rendering\n");
        return;
    }
    
    mpv_render_context_set_update_callback(w->mpv_ctx, on_mpv_update, w);
}

void WorkerDestroy(FrameWorker *w) {
    if (w->mpv_ctx) {
        mpv_render_context_free(w->mpv_ctx);
        w->mpv_ctx = NULL;
    }
    if (w->mpv) {
        mpv_terminate_destroy(w->mpv);
        w->mpv = NULL;
    }
}

void UpdateVideoFrame(AppState *s) {
    if (s->currentIndex < 0) return;
    MediaItem *item = &s->items[s->currentIndex];
    if (!item->metaLoaded) ProbeMedia(item);
    
    if (s->currentFrame < 0) s->currentFrame = 0;
    if (item->totalFrames > 0 && s->currentFrame > item->totalFrames)
        s->currentFrame = item->totalFrames;
        
    float t = (item->fps > 0) ? (float)s->currentFrame / item->fps : s->currentVideoTime;
    s->currentVideoTime = t;

    char start_time[32];
    snprintf(start_time, sizeof(start_time), "%.3f", t);

    mpv_set_property_string(s->worker.mpv, "start", start_time);
    const char *cmd[] = {"loadfile", item->fullPath, "replace", NULL};
    int r = mpv_command(s->worker.mpv, cmd);
    if (r < 0) {
        fprintf(stderr, "ERROR: mpv_command failed with %d\n", r);
    }
    mpv_set_property_string(s->worker.mpv, "pause", "yes");
}

void RequestFrame(AppState *s, float t, bool accurate) {
    if (s->currentIndex < 0 || !s->worker.mpv) return;
    
    double t_double = (double)t;
    mpv_set_property_async(s->worker.mpv, 0, "time-pos", MPV_FORMAT_DOUBLE, &t_double);
}

bool WorkerPollFrame(AppState *s) {
    FrameWorker *w = &s->worker;
    if (!w->mpv || !w->mpv_ctx) return false;

    while (1) {
        mpv_event *event = mpv_wait_event(w->mpv, 0);
        if (event->event_id == MPV_EVENT_NONE) break;
    }

    uint64_t flags = mpv_render_context_update(w->mpv_ctx);
    if (flags & MPV_RENDER_UPDATE_FRAME) {
        MediaItem *item = &s->items[s->currentIndex];
        int pw = item->width;
        int ph = item->height;
        if (pw <= 0 || ph <= 0) return false;

        int sz = pw * ph * 4;
        unsigned char *buf = malloc(sz);
        if (!buf) return false;

        size_t pitch = pw * 4;
        int size[2] = {pw, ph};
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_SW_SIZE, size},
            {MPV_RENDER_PARAM_SW_FORMAT, "rgba"},
            {MPV_RENDER_PARAM_SW_STRIDE, &pitch},
            {MPV_RENDER_PARAM_SW_POINTER, buf},
            {MPV_RENDER_PARAM_INVALID, NULL}
        };

        // Render straight into our buffer!
        if (mpv_render_context_render(w->mpv_ctx, params) >= 0) {
            if (s->textureLoaded && s->currentTexture.width == pw && s->currentTexture.height == ph) {
                UpdateTexture(s->currentTexture, buf);
            } else {
                if (s->textureLoaded) UnloadTexture(s->currentTexture);
                Image img = {buf, pw, ph, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
                s->currentTexture = LoadTextureFromImage(img);
                s->textureLoaded = true;
            }
        }
        free(buf);
        return true;
    }
    return false;
}


void PreviewInFfplay(MediaItem *item){
    int vW=(item->rotation%180==0)?item->width :item->height;
    int vH=(item->rotation%180==0)?item->height:item->width;

    char filters[512]={0};
    if     (item->rotation== 90)strcpy(filters,"transpose=1,");
    else if(item->rotation==180)strcpy(filters,"transpose=1,transpose=1,");
    else if(item->rotation==270)strcpy(filters,"transpose=2,");

    bool fullCrop = (abs((int)item->cropRect.width - vW) < 2 && 
                     abs((int)item->cropRect.height - vH) < 2 &&
                     item->cropRect.x < 1 && item->cropRect.y < 1);
                     
    if(!fullCrop){
        char cf[128];
        snprintf(cf,sizeof(cf),"crop=%d:%d:%d:%d",
            (int)item->cropRect.width,(int)item->cropRect.height,
            (int)item->cropRect.x,   (int)item->cropRect.y);
        strcat(filters,cf);
    } else {
        int l=strlen(filters);
        if(l>0&&filters[l-1]==',')filters[l-1]='\0';
    }

    float dur = item->trimEnd - item->trimStart;
    int s_sec = (int)item->trimStart;
    int s_ms = (int)((item->trimStart - s_sec) * 1000.0f);
    int d_sec = (int)dur;
    int d_ms = (int)((dur - d_sec) * 1000.0f);

    char cmd[MAX_PATH_LEN * 4];
    if(filters[0])
        snprintf(cmd,sizeof(cmd), "ffplay -ss %d.%03d -t %d.%03d -i \"%s\" -vf \"%s\" > /dev/null 2>&1 &", 
                 s_sec, s_ms, d_sec, d_ms, item->fullPath, filters);
    else
        snprintf(cmd,sizeof(cmd), "ffplay -ss %d.%03d -t %d.%03d -i \"%s\" > /dev/null 2>&1 &", 
                 s_sec, s_ms, d_sec, d_ms, item->fullPath);
    system(cmd);
}

void ProcessItem(AppState *state, MediaItem *item) {
    if (item->skip || item->isDeleted) {
        AddLog(state, TextFormat("Skipped: %s", item->fileName), GRAY);
        return;
    }
    EnsureOutputDir(state->workingDir);
    
    char outPath[MAX_PATH_LEN * 2];
    const char *ext = GetExtension(item->fullPath);
    const char *name = GetFileNameWithoutExt(item->fullPath);
    snprintf(outPath, sizeof(outPath), "%s/%s/%s_edit.%s", state->workingDir, OUTPUT_DIR_NAME, name, ext);

    int visW = (item->rotation % 180 == 0) ? item->width : item->height;
    int visH = (item->rotation % 180 == 0) ? item->height : item->width;

    if (item->cropRect.width <= 0) {
        if (!item->metaLoaded) ProbeMedia(item);
        item->cropRect = (Rectangle){0,0, (float)visW, (float)visH};
    }
    ClampCrop(&item->cropRect, visW, visH);

    char filters[512] = {0};
    if (item->rotation == 90) strcpy(filters, "transpose=1,");       
    else if (item->rotation == 180) strcpy(filters, "transpose=1,transpose=1,"); 
    else if (item->rotation == 270) strcpy(filters, "transpose=2,"); 
    
    char cropFilter[128];
    snprintf(cropFilter, 128, "crop=%d:%d:%d:%d", 
        (int)item->cropRect.width, (int)item->cropRect.height,
        (int)item->cropRect.x, (int)item->cropRect.y);
    strcat(filters, cropFilter);

    bool fullCrop = (abs((int)item->cropRect.width - visW) < 2 && 
                     abs((int)item->cropRect.height - visH) < 2 &&
                     item->cropRect.x < 1 && item->cropRect.y < 1);
    
    bool fullDuration = true;
    if (item->type == MEDIA_TYPE_VIDEO) {
        if (item->trimStart > 0.1f || (item->duration - item->trimEnd) > 0.1f) fullDuration = false;
    }

    int s_sec = (int)item->trimStart;
    int s_ms = (int)((item->trimStart - s_sec) * 1000.0f);
    int e_sec = (int)item->trimEnd;
    int e_ms = (int)((item->trimEnd - e_sec) * 1000.0f);
    fprintf(stderr, "DEBUG ProcessItem: trimStart=%f trimEnd=%f duration=%f\n", 
            item->trimStart, item->trimEnd, item->duration);
    fprintf(stderr, "DEBUG ProcessItem: e_sec=%d e_ms=%d\n", e_sec, e_ms);

    char cmd[MAX_PATH_LEN * 4];
    
    if (item->rotation == 0 && fullCrop && fullDuration && item->type == MEDIA_TYPE_VIDEO) {
        snprintf(cmd, sizeof(cmd), "ffmpeg -y -i \"%s\" -c copy \"%s\" < /dev/null > /dev/null 2>&1", item->fullPath, outPath);
        AddLog(state, TextFormat("Stream Copy: %s", item->fileName), SKYBLUE);
    } 
    else if (item->type == MEDIA_TYPE_IMAGE && item->rotation == 0 && fullCrop) {
        snprintf(cmd, sizeof(cmd), "cp \"%s\" \"%s\"", item->fullPath, outPath);
        AddLog(state, TextFormat("Copy: %s", item->fileName), SKYBLUE);
    }
    else {
        if (item->type == MEDIA_TYPE_IMAGE) {
            snprintf(cmd, sizeof(cmd), 
                "ffmpeg -y -i \"%s\" -vf \"%s\" \"%s\" < /dev/null > /dev/null 2>&1",
                item->fullPath, filters, outPath);
        } else {
            snprintf(cmd, sizeof(cmd), 
                "ffmpeg -y -ss %d.%03d -to %d.%03d -i \"%s\" -vf \"%s\" -c:a copy \"%s\" < /dev/null > /dev/null 2>&1",
                s_sec, s_ms, e_sec, e_ms, item->fullPath, filters, outPath);
        }
        AddLog(state, TextFormat("Processing: %s", item->fileName), PROCESS_COLOR);
    }
    
    if (system(cmd) == 0) item->isProcessed = true;
    else AddLog(state, TextFormat("Error: %s", item->fileName), RED);
}

void DeleteOriginal(AppState *state, MediaItem *item) {
    if (item->isDeleted) return;
    if (remove(item->fullPath) == 0) {
        AddLog(state, TextFormat("Deleted: %s", item->fileName), DELETE_COLOR);
        item->isDeleted = true;
    } else AddLog(state, "Error deleting file", RED);
}
