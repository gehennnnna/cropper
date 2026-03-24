#ifndef COMMON_H
#define COMMON_H

#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>

// Include libmpv headers
#include <mpv/client.h>
#include <mpv/render.h>

#define MAX_PATH_LEN    4096
#define SAVE_VERSION    7
#define STATE_FILENAME  ".cropper.dat"
#define OUTPUT_DIR_NAME "processed"
#define MAX_LOGS        100

static const Color PROCESS_COLOR = {40,180,40,255};
static const Color DELETE_COLOR  = {180,40,40,255};

typedef enum { MEDIA_TYPE_UNKNOWN=0, MEDIA_TYPE_IMAGE, MEDIA_TYPE_VIDEO } MediaType;

typedef struct {
    char      fullPath[MAX_PATH_LEN];
    char      fileName[256];
    MediaType type;
    int       width, height;
    float     duration, fps;
    int       totalFrames;
    bool      metaLoaded;
    Rectangle cropRect;
    float     trimStart, trimEnd;
    int       rotation;
    bool      skip, touched, isProcessed, isDeleted, isMarkedForDeletion;
} MediaItem;

typedef struct {
    char  messages[MAX_LOGS][256];
    Color colors[MAX_LOGS];
    int   count, scrollOffset;
} LogSystem;

// \u2500\u2500 libmpv integration \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500
typedef struct {
    mpv_handle *mpv;
    mpv_render_context *mpv_ctx;
    volatile bool pixelsReady;
} FrameWorker;

typedef struct {
    MediaItem *items;
    int count, capacity, currentIndex;
    char workingDir[MAX_PATH_LEN];

    Texture2D currentTexture;
    bool      textureLoaded;
    float     currentVideoTime;
    int       currentFrame;

    float   baseScale, zoom;
    Vector2 pan, baseOffset;

    int  activeTextInput;
    char inputBuffer[32];
    bool isDraggingCrop;
    int  cropHandleIndex;
    bool isDraggingTime, isDraggingTrimStart, isDraggingTrimEnd;

    bool isProcessingBatch;
    int  batchIndex;
    bool showLogWindow;
    LogSystem logs;

    FrameWorker worker;
} AppState;

static inline const char* GetExtension(const char *p){
    const char *d=strrchr(p,'.'); return (!d||d==p)?"":(d+1);
}
static inline bool HasExt(const char *p,const char *e){ return strcasecmp(GetExtension(p),e)==0; }
static inline bool IsVideo(const char *p){
    return HasExt(p,"mp4")||HasExt(p,"avi")||HasExt(p,"mov")||
           HasExt(p,"mkv")||HasExt(p,"webm");
}
static inline bool IsImage(const char *p){
    return HasExt(p,"png")||HasExt(p,"jpg")||HasExt(p,"jpeg")||HasExt(p,"bmp");
}
static inline void ClampCrop(Rectangle *c,int W,int H){
    if(W<=0||H<=0)return;
    if(c->x<0)  c->x=0;
    if(c->y<0)  c->y=0;
    if(c->x>=W) c->x=0;
    if(c->y>=H) c->y=0;
    if(c->width >W) c->width =(float)W;
    if(c->height>H) c->height=(float)H;
    if(c->x+c->width >W){c->x=W-c->width; if(c->x<0){c->x=0;c->width=(float)W;}}
    if(c->y+c->height>H){c->y=H-c->height;if(c->y<0){c->y=0;c->height=(float)H;}}
    if(c->width <10)c->width =10;
    if(c->height<10)c->height=10;
}
static inline const char* FormatTime(float t){
    int m=(int)t/60,s=(int)t%60,ms=(int)((t-(int)t)*1000);
    static char b[32]; snprintf(b,sizeof(b),"%02d:%02d.%03d",m,s,ms); return b;
}

// \u2500\u2500 Function Prototypes (Fixes GCC Implicit Declarations) \u2500\u2500
// media.c
void AddLog(AppState *state, const char *msg, Color col);
void EnsureOutputDir(const char *workDir);
void ProbeMedia(MediaItem *item);
void WorkerInit(FrameWorker *w);
void WorkerDestroy(FrameWorker *w);
void UpdateVideoFrame(AppState *s);
void RequestFrame(AppState *s, float t, bool accurate);
bool WorkerPollFrame(AppState *s);
void PreviewInFfplay(MediaItem *item);
void ProcessItem(AppState *state, MediaItem *item);
void DeleteOriginal(AppState *state, MediaItem *item);

// state.c
void SaveState(AppState *state);
bool LoadState(AppState *state);
void AddFile(AppState *state, const char *path);
void LoadActiveMedia(AppState *state);

#endif
