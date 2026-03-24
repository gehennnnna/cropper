#include "common.h"
#include "raymath.h"
#include <math.h>

extern void LoadActiveMedia(AppState *state);
extern void AddFile(AppState *state, const char *path);
extern void SaveState(AppState *state);
extern bool LoadState(AppState *state);
extern void ProcessItem(AppState *state, MediaItem *item);
extern void UpdateVideoFrame(AppState *state);
extern void RequestFrame(AppState *state, float t, bool accurate);
extern bool WorkerPollFrame(AppState *state);
extern void WorkerInit(FrameWorker *w);
extern void WorkerDestroy(FrameWorker *w);
extern void PreviewInFfplay(MediaItem *item);
extern void DeleteOriginal(AppState *state, MediaItem *item);
extern void AddLog(AppState *state, const char *msg, Color col);
extern void ProbeMedia(MediaItem *item);

#define C_BG     ((Color){13, 13, 16,255})
#define C_PANEL  ((Color){19, 19, 24,255})
#define C_BORDER ((Color){38, 38, 50,255})
#define C_BTN    ((Color){30, 30, 40,255})
#define C_BTHOV  ((Color){44, 44, 58,255})
#define C_TEXT   ((Color){220,222,230,255})
#define C_DIM    ((Color){90, 94,110,255})
#define C_ACCENT ((Color){61,143,245,255})
#define C_GREEN  ((Color){48,190, 90,255})
#define C_RED    ((Color){220, 55, 55,255})
#define C_AMBER  ((Color){240,175, 30,255})
#define C_CORAL  ((Color){235, 80, 70,255})
#define C_SKIP   ((Color){160, 70, 30,255})

#define SIDEBAR_W 272
#define TL_H       56
#define FS_SM      12
#define FS_MD      14

static Font G_FONT={0};
static bool G_FONT_OK=false;
static const char *INTER_PATHS[]={
    "./Inter-Regular.ttf",
    "/usr/share/fonts/truetype/inter/Inter-Regular.ttf",
    "/usr/share/fonts/inter/Inter-Regular.ttf",
    "/usr/share/fonts/web/inter.ttf",
    "/usr/share/fonts/opentype/inter/Inter-Regular.otf",
    "/usr/local/share/fonts/Inter-Regular.ttf",
    NULL
};
static void LoadInterFont(void){
    for(int i=0;INTER_PATHS[i];i++){
        if(FileExists(INTER_PATHS[i])){
            G_FONT=LoadFontEx(INTER_PATHS[i],32,NULL,250);
            if(G_FONT.texture.id>0){
                G_FONT_OK=true;
                SetTextureFilter(G_FONT.texture,TEXTURE_FILTER_BILINEAR);
                return;
            }
        }
    }
    G_FONT=GetFontDefault();
}
static void Txt(const char *t,int x,int y,int sz,Color c){
    DrawTextEx(G_FONT,t,(Vector2){(float)x,(float)y},(float)sz,sz*0.06f,c);
}
static int TxtW(const char *t,int sz){
    return (int)MeasureTextEx(G_FONT,t,(float)sz,sz*0.06f).x;
}

static bool IsHov(Rectangle r){return CheckCollisionPointRec(GetMousePosition(),r);}
static bool Btn(Rectangle r,const char *t,Color bg){
    DrawRectangleRec(r,IsHov(r)?C_BTHOV:bg);
    DrawRectangleLinesEx(r,1,C_BORDER);
    Txt(t,(int)(r.x+(r.width-TxtW(t,FS_MD))/2),(int)(r.y+(r.height-FS_MD)/2),FS_MD,C_TEXT);
    return IsHov(r)&&IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}
static bool BtnC(Rectangle r,const char *t,Color bg,Color fg){
    DrawRectangleRec(r,IsHov(r)?(Color){bg.r+18,bg.g+18,bg.b+18,255}:bg);
    Txt(t,(int)(r.x+(r.width-TxtW(t,FS_MD))/2),(int)(r.y+(r.height-FS_MD)/2),FS_MD,fg);
    return IsHov(r)&&IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}
static void HLine(int x,int y,int w){DrawRectangle(x,y,w,1,C_BORDER);}
static void Cap(const char *t,int x,int y){Txt(t,x,y,FS_SM,C_DIM);}

static Rectangle VRect(AppState *s,Rectangle r){
    float sc=s->baseScale*s->zoom;
    return (Rectangle){s->baseOffset.x+s->pan.x+r.x*sc,
                       s->baseOffset.y+s->pan.y+r.y*sc,r.width*sc,r.height*sc};
}
static void RecalcOff(AppState *s,float vpW,float vpH,float vW,float vH){
    s->baseOffset.x=(vpW-vW*s->baseScale*s->zoom)/2.f;
    s->baseOffset.y=(vpH-vH*s->baseScale*s->zoom)/2.f;
}
static void FitView(AppState *s,float vpW,float vpH,float vW,float vH){
    s->zoom=1.f;s->pan=(Vector2){0,0};
    float sw=(vpW-40)/vW,sh=(vpH-40)/vH;
    s->baseScale=sw<sh?sw:sh;
    RecalcOff(s,vpW,vpH,vW,vH);
}
static void StepFrame(AppState *s,int d){
    MediaItem *it=&s->items[s->currentIndex];
    if(!it->metaLoaded)ProbeMedia(it);
    s->currentFrame=Clamp(s->currentFrame+d,0,it->totalFrames);
    float t=(it->fps>0)?(float)s->currentFrame/it->fps:s->currentVideoTime;
    s->currentVideoTime=t;
    RequestFrame(s,t,true);  // accurate (2s window) for frame step
}

int main(int argc,char *argv[]){
    SetConfigFlags(FLAG_WINDOW_RESIZABLE|FLAG_MSAA_4X_HINT);
    InitWindow(1380,880,"cropper");
    SetTargetFPS(60);
    LoadInterFont();

    AppState state={0};
    state.currentIndex=-1;state.activeTextInput=-1;
    state.zoom=1.f;state.cropHandleIndex=-1;
    WorkerInit(&state.worker);

    bool isDir=true;const char *dir=".";
    if(argc>1){if(DirectoryExists(argv[1]))dir=argv[1];else isDir=false;}
    snprintf(state.workingDir,MAX_PATH_LEN,"%s",dir);

    if(isDir){
        if(!LoadState(&state)){
            FilePathList fl=LoadDirectoryFiles(state.workingDir);
            for(int k=0;k<(int)fl.count;k++)AddFile(&state,fl.paths[k]);
            UnloadDirectoryFiles(fl);
            if(state.count>0){state.currentIndex=0;LoadActiveMedia(&state);}
        } else LoadActiveMedia(&state);
    } else {
        for(int i=1;i<argc;i++)if(FileExists(argv[i]))AddFile(&state,argv[i]);
        if(state.count>0){state.currentIndex=0;LoadActiveMedia(&state);}
    }

    bool handMode=false;

    while(!WindowShouldClose()){
        float winW=GetScreenWidth(),winH=GetScreenHeight();
        float vpW=winW-SIDEBAR_W,vpH=winH;
        MediaItem *cur=(state.currentIndex>=0&&state.currentIndex<state.count)
                       ?&state.items[state.currentIndex]:NULL;
        bool isVideo=cur&&cur->type==MEDIA_TYPE_VIDEO;
        if(isVideo)vpH-=TL_H;

        WorkerPollFrame(&state);

        if(state.isProcessingBatch){
            state.showLogWindow=true;
            if(state.batchIndex<state.count)
                ProcessItem(&state,&state.items[state.batchIndex++]);
            else{state.isProcessingBatch=false;AddLog(&state,"Done.",C_GREEN);SaveState(&state);}
        }

        if(cur&&!cur->isDeleted&&state.textureLoaded&&!state.isProcessingBatch){
            float vW=(cur->rotation%180==0)?cur->width :cur->height;
            float vH=(cur->rotation%180==0)?cur->height:cur->width;
            {float sw=(vpW-40)/vW,sh=(vpH-40)/vH;state.baseScale=sw<sh?sw:sh;}
            RecalcOff(&state,vpW,vpH,vW,vH);

            Vector2 mouse=GetMousePosition();
            bool overVP=mouse.x<vpW&&mouse.y>=0&&mouse.y<vpH;

            if(IsKeyPressed(KEY_H))handMode=!handMode;
            if(IsKeyPressed(KEY_SPACE))FitView(&state,vpW,vpH,vW,vH);

            if(overVP&&!state.isDraggingCrop){
                float wh=GetMouseWheelMove();
                if(wh!=0){
                    float oldZ=state.zoom,newZ=Clamp(oldZ*(1.f+wh*0.13f),0.1f,20.f);
                    float sc0=state.baseScale*oldZ;
                    float iX=(mouse.x-state.baseOffset.x-state.pan.x)/sc0;
                    float iY=(mouse.y-state.baseOffset.y-state.pan.y)/sc0;
                    state.zoom=newZ;
                    float nBx=(vpW-vW*state.baseScale*newZ)/2.f;
                    float nBy=(vpH-vH*state.baseScale*newZ)/2.f;
                    state.pan.x=mouse.x-nBx-iX*state.baseScale*newZ;
                    state.pan.y=mouse.y-nBy-iY*state.baseScale*newZ;
                    state.baseOffset.x=nBx;state.baseOffset.y=nBy;
                }
            }

            bool panBtn=IsMouseButtonDown(MOUSE_BUTTON_RIGHT)||
                        (handMode&&IsMouseButtonDown(MOUSE_BUTTON_LEFT));
            if(panBtn&&overVP&&!state.isDraggingCrop&&
               !state.isDraggingTime&&!state.isDraggingTrimStart&&!state.isDraggingTrimEnd)
                state.pan=Vector2Add(state.pan,GetMouseDelta());

            if(!handMode){
                Rectangle sR=VRect(&state,cur->cropRect);
                Vector2 hv[8]={
                    {sR.x,sR.y},{sR.x+sR.width/2,sR.y},{sR.x+sR.width,sR.y},
                    {sR.x+sR.width,sR.y+sR.height/2},{sR.x+sR.width,sR.y+sR.height},
                    {sR.x+sR.width/2,sR.y+sR.height},{sR.x,sR.y+sR.height},
                    {sR.x,sR.y+sR.height/2}
                };
                if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&
                   !state.isDraggingTime&&!state.isDraggingTrimStart&&!state.isDraggingTrimEnd){
                    for(int i=0;i<8;i++)
                        if(CheckCollisionPointCircle(mouse,hv[i],10))
                            {state.isDraggingCrop=true;state.cropHandleIndex=i;break;}
                    if(!state.isDraggingCrop&&CheckCollisionPointRec(mouse,sR))
                        {state.isDraggingCrop=true;state.cropHandleIndex=8;}
                }
                if(state.isDraggingCrop){
                    if(IsMouseButtonReleased(MOUSE_BUTTON_LEFT)){state.isDraggingCrop=false;}
                    else{
                        cur->touched=true;cur->skip=false;
                        Vector2 d=GetMouseDelta();float sc=state.baseScale*state.zoom;
                        d.x/=sc;d.y/=sc;
                        Rectangle *c=&cur->cropRect;
                        switch(state.cropHandleIndex){
                            case 0:c->x+=d.x;c->y+=d.y;c->width-=d.x;c->height-=d.y;break;
                            case 1:c->y+=d.y;c->height-=d.y;break;
                            case 2:c->y+=d.y;c->width+=d.x;c->height-=d.y;break;
                            case 3:c->width+=d.x;break;
                            case 4:c->width+=d.x;c->height+=d.y;break;
                            case 5:c->height+=d.y;break;
                            case 6:c->x+=d.x;c->width-=d.x;c->height+=d.y;break;
                            case 7:c->x+=d.x;c->width-=d.x;break;
                            case 8:c->x+=d.x;c->y+=d.y;break;
                        }
                        ClampCrop(c,(int)vW,(int)vH);
                    }
                }
            }

            if(isVideo){
                float fps=cur->fps>0?cur->fps:30.f;
                bool sh2=IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT);
                if(IsKeyPressed(KEY_LEFT) ||IsKeyPressedRepeat(KEY_LEFT)) StepFrame(&state,sh2?-10:-1);
                if(IsKeyPressed(KEY_RIGHT)||IsKeyPressedRepeat(KEY_RIGHT))StepFrame(&state,sh2? 10: 1);
                if(IsKeyPressed(KEY_I)){
                    cur->trimStart=Clamp(roundf(state.currentVideoTime*fps)/fps,0,cur->trimEnd-1.f/fps);
                    cur->touched=true;cur->skip=false;
                    AddLog(&state,TextFormat("In  f%d  %.3fs",state.currentFrame,cur->trimStart),C_AMBER);
                }
                if(IsKeyPressed(KEY_O)){
                    cur->trimEnd=Clamp(roundf(state.currentVideoTime*fps)/fps,cur->trimStart+1.f/fps,cur->duration);
                    cur->touched=true;cur->skip=false;
                    AddLog(&state,TextFormat("Out f%d  %.3fs",state.currentFrame,cur->trimEnd),C_CORAL);
                }

                float tlY=vpH,ppSec=vpW/cur->duration;
                Rectangle rS={cur->trimStart*ppSec-6,tlY,12,TL_H};
                Rectangle rE={cur->trimEnd  *ppSec-6,tlY,12,TL_H};
                Rectangle rB={0,tlY,vpW,TL_H};
                if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&!state.isDraggingCrop){
                    if     (CheckCollisionPointRec(mouse,rS))state.isDraggingTrimStart=true;
                    else if(CheckCollisionPointRec(mouse,rE))state.isDraggingTrimEnd=true;
                    else if(CheckCollisionPointRec(mouse,rB))state.isDraggingTime=true;
                }
                if(IsMouseButtonReleased(MOUSE_BUTTON_LEFT)){
                    if(state.isDraggingTime){
                        // On release: accurate seek snapped to frame
                        state.currentFrame=(int)roundf(state.currentVideoTime*fps);
                        state.currentVideoTime=(float)state.currentFrame/fps;
                        RequestFrame(&state,state.currentVideoTime,true);
                    }
                    state.isDraggingTime=state.isDraggingTrimStart=state.isDraggingTrimEnd=false;
                }
                if(state.isDraggingTrimStart||state.isDraggingTrimEnd||state.isDraggingTime){
                    float d=GetMouseDelta().x/ppSec;
                    if(state.isDraggingTrimStart){
                        cur->trimStart=Clamp(roundf((cur->trimStart+d)*fps)/fps,0,cur->trimEnd-1.f/fps);
                        cur->touched=true;cur->skip=false;
                    }
                    if(state.isDraggingTrimEnd){
                        cur->trimEnd=Clamp(roundf((cur->trimEnd+d)*fps)/fps,cur->trimStart+1.f/fps,cur->duration);
                        cur->touched=true;cur->skip=false;
                    }
                    if(state.isDraggingTime){
                        state.currentVideoTime=Clamp(state.currentVideoTime+d,0,cur->duration);
                        // Fast keyframe seek \u2014 instant anywhere in the video
                        RequestFrame(&state,state.currentVideoTime,false);
                    }
                }
            }
        }

        BeginDrawing();
        ClearBackground(C_BG);
        SetMouseCursor(handMode?MOUSE_CURSOR_POINTING_HAND:MOUSE_CURSOR_DEFAULT);

        if(cur&&!cur->isDeleted&&state.textureLoaded){
            float vW=(cur->rotation%180==0)?cur->width :cur->height;
            float vH=(cur->rotation%180==0)?cur->height:cur->width;
            float eff=state.baseScale*state.zoom;
            Rectangle dest={
                state.baseOffset.x+state.pan.x+vW*eff/2.f,
                state.baseOffset.y+state.pan.y+vH*eff/2.f,
                cur->width*eff,cur->height*eff
            };
            DrawTexturePro(state.currentTexture,
                (Rectangle){0,0,(float)state.currentTexture.width,(float)state.currentTexture.height},
                dest,(Vector2){dest.width/2.f,dest.height/2.f},(float)cur->rotation,
                cur->skip?Fade(WHITE,0.2f):WHITE);

            if(!cur->skip){
                Rectangle sR=VRect(&state,cur->cropRect);
                float cw=cur->cropRect.width*eff,ch=cur->cropRect.height*eff;
                float cx=state.baseOffset.x+state.pan.x+cur->cropRect.x*eff;
                float cy=state.baseOffset.y+state.pan.y+cur->cropRect.y*eff;
                Color ov={0,0,0,100};
                DrawRectangle(0,0,(int)vpW,(int)cy,ov);
                DrawRectangle(0,(int)(cy+ch),(int)vpW,(int)(vpH-cy-ch),ov);
                DrawRectangle(0,(int)cy,(int)cx,(int)ch,ov);
                DrawRectangle((int)(cx+cw),(int)cy,(int)(vpW-cx-cw),(int)ch,ov);
                DrawRectangleLinesEx(sR,1.f,C_ACCENT);
                Color th={61,143,245,30};
                for(int n=1;n<3;n++){
                    DrawLine((int)(cx+cw*n/3),(int)cy,(int)(cx+cw*n/3),(int)(cy+ch),th);
                    DrawLine((int)cx,(int)(cy+ch*n/3),(int)(cx+cw),(int)(cy+ch*n/3),th);
                }
                Txt(TextFormat("%d�%d",(int)cur->cropRect.width,(int)cur->cropRect.height),
                    (int)sR.x+2,(int)sR.y-16,FS_SM,C_ACCENT);
                Vector2 hv[8]={
                    {sR.x,sR.y},{sR.x+sR.width/2,sR.y},{sR.x+sR.width,sR.y},
                    {sR.x+sR.width,sR.y+sR.height/2},{sR.x+sR.width,sR.y+sR.height},
                    {sR.x+sR.width/2,sR.y+sR.height},{sR.x,sR.y+sR.height},
                    {sR.x,sR.y+sR.height/2}
                };
                for(int i=0;i<8;i++){
                    bool act=state.isDraggingCrop&&state.cropHandleIndex==i;
                    DrawCircle((int)hv[i].x,(int)hv[i].y,5,(Color){0,0,0,180});
                    DrawCircle((int)hv[i].x,(int)hv[i].y,3,act?C_ACCENT:C_TEXT);
                }
            } else {
                const char *sk="SKIP";
                Txt(sk,(int)(vpW/2-TxtW(sk,36)/2),(int)(vpH/2-18),36,C_RED);
            }
            Txt(TextFormat("%.0f%%  [H]hand  [Space]fit  [RMB]pan",state.zoom*100),
                10,(int)(vpH-20),FS_SM,C_DIM);

            if(isVideo){
                float tlY=vpH,fps=cur->fps>0?cur->fps:30.f,ppSec=vpW/cur->duration;
                DrawRectangle(0,(int)tlY,(int)vpW,TL_H,(Color){10,10,14,255});
                DrawRectangle(0,(int)tlY,(int)vpW,1,C_BORDER);
                float sx=cur->trimStart*ppSec,ex=cur->trimEnd*ppSec;
                DrawRectangle((int)sx,(int)tlY,(int)(ex-sx),TL_H,(Color){61,143,245,22});
                int totF=cur->totalFrames>0?cur->totalFrames:(int)(cur->duration*fps);
                float ppf=vpW/(float)(totF>0?totF:1);
                int step=1; while(ppf*step<5)step=step<10?10:step+10;
                for(int f=0;f<=totF;f+=step){
                    float tx=(f/fps)*ppSec;bool maj=(f%(step*5)==0);
                    DrawLine((int)tx,(int)(tlY+(maj?2:12)),(int)tx,(int)(tlY+TL_H-2),
                             maj?(Color){60,65,80,220}:(Color){40,44,55,150});
                    if(maj&&ppf*step>18)Txt(FormatTime(f/fps),(int)tx+2,(int)(tlY+3),10,C_DIM);
                }
                DrawRectangle((int)sx-2,(int)tlY,3,TL_H,C_AMBER);
                DrawRectangle((int)ex-1,(int)tlY,3,TL_H,C_CORAL);
                Txt(TextFormat("I%d",(int)roundf(cur->trimStart*fps)),(int)sx+4,(int)(tlY+3),10,C_AMBER);
                Txt(TextFormat("O%d",(int)roundf(cur->trimEnd*fps)), (int)ex+4,(int)(tlY+3),10,C_CORAL);
                float hx=state.currentVideoTime*ppSec;
                Color phC=state.isDraggingTime?C_ACCENT:C_TEXT;
                DrawRectangle((int)hx-1,(int)tlY,2,TL_H,phC);
                DrawTriangle((Vector2){hx,tlY},(Vector2){hx-5,tlY-7},(Vector2){hx+5,tlY-7},phC);
                int inF=(int)roundf(cur->trimStart*fps),outF=(int)roundf(cur->trimEnd*fps);
                const char *tr=TextFormat("%s  f%d  |  sel %df  %s",
                    FormatTime(state.currentVideoTime),state.currentFrame,
                    outF-inF,FormatTime(cur->trimEnd-cur->trimStart));
                Txt(tr,(int)(vpW/2-TxtW(tr,11)/2),(int)(tlY+TL_H-16),11,C_DIM);
            }
        } else {
            const char *m=cur?"FILE DELETED":"No files loaded.";
            Txt(m,(int)(vpW/2-TxtW(m,16)/2),(int)(vpH/2),16,C_DIM);
        }

        DrawRectangle((int)vpW,0,SIDEBAR_W,(int)winH,C_PANEL);
        DrawRectangle((int)vpW,0,1,(int)winH,C_BORDER);
        int px=(int)vpW+14,py=14,bw=SIDEBAR_W-28;

        if(cur&&!cur->isDeleted){
            Txt(cur->fileName,px,py,FS_SM,C_DIM);py+=18;
            if(cur->metaLoaded){
                int vw2=(cur->rotation%180==0)?cur->width:cur->height;
                int vh2=(cur->rotation%180==0)?cur->height:cur->width;
                Txt(TextFormat("%d�%d  #%d/%d",vw2,vh2,state.currentIndex+1,state.count),px,py,FS_SM,C_TEXT);py+=16;
                if(isVideo){Txt(TextFormat("%.2f fps  %s",cur->fps,FormatTime(cur->duration)),px,py,FS_SM,C_DIM);py+=16;}
            }
            py+=4;HLine(px-4,py,bw+8);py+=10;

            Cap("TRANSFORM",px,py);py+=14;
            if(Btn((Rectangle){px,py,bw,28},"Rotate 90� CW",C_BTN)){
                cur->rotation=(cur->rotation+90)%360;
                int vw3=(cur->rotation%180==0)?cur->width:cur->height;
                int vh3=(cur->rotation%180==0)?cur->height:cur->width;
                cur->cropRect=(Rectangle){0,0,(float)vw3,(float)vh3};cur->touched=true;
            }
            py+=34;HLine(px-4,py,bw+8);py+=10;

            if(isVideo){
                Cap("FRAME  [\u2190\u2192] Shift=�10  [I]in [O]out",px,py);py+=14;
                int fw=(bw-9)/4;
                if(Btn((Rectangle){px,       py,fw,26},"<<10",C_BTN))StepFrame(&state,-10);
                if(Btn((Rectangle){px+fw+3,  py,fw,26},"< 1", C_BTN))StepFrame(&state,-1);
                if(Btn((Rectangle){px+fw*2+6,py,fw,26},"1 >", C_BTN))StepFrame(&state, 1);
                if(Btn((Rectangle){px+fw*3+9,py,fw,26},"10>>",C_BTN))StepFrame(&state,10);
                py+=32;
                int hw=(bw-4)/2;
                if(BtnC((Rectangle){px,      py,hw,28},"[I] Set In", C_BTN,C_AMBER)){
                    float fps=cur->fps>0?cur->fps:30.f;
                    cur->trimStart=Clamp(roundf(state.currentVideoTime*fps)/fps,0,cur->trimEnd-1.f/fps);
                    cur->touched=true;cur->skip=false;
                }
                if(BtnC((Rectangle){px+hw+4,py,hw,28},"Set Out [O]",C_BTN,C_CORAL)){
                    float fps=cur->fps>0?cur->fps:30.f;
                    cur->trimEnd=Clamp(roundf(state.currentVideoTime*fps)/fps,cur->trimStart+1.f/fps,cur->duration);
                    cur->touched=true;cur->skip=false;
                }
                py+=36;
                if(BtnC((Rectangle){px,py,bw,28},"\u25b6  Play in ffplay",C_BTN,C_GREEN))
                    PreviewInFfplay(cur);
                py+=34;HLine(px-4,py,bw+8);py+=10;
            }

            Cap("ITEM",px,py);py+=14;
            if(BtnC((Rectangle){px,py,bw,30},cur->skip?"Unskip":"Skip",cur->skip?C_SKIP:C_BTN,C_TEXT)){
                cur->skip=!cur->skip;if(!cur->skip)cur->touched=true;
            }
            py+=36;
            if(BtnC((Rectangle){px,py,bw,28},"Delete Original",C_BTN,C_RED))
                DeleteOriginal(&state,cur);
            py+=34;HLine(px-4,py,bw+8);py+=10;

            Cap("NAVIGATE",px,py);py+=14;
            int nw=(bw-4)/2;
            if(Btn((Rectangle){px,      py,nw,30},"< Prev",C_BTN)){
                if(!cur->touched&&!cur->skip)cur->skip=true;
                if(state.currentIndex>0){state.currentIndex--;state.currentFrame=0;LoadActiveMedia(&state);SaveState(&state);}
            }
            if(Btn((Rectangle){px+nw+4,py,nw,30},"Next >",C_BTN)){
                if(!cur->touched&&!cur->skip)cur->skip=true;
                if(state.currentIndex<state.count-1){state.currentIndex++;state.currentFrame=0;LoadActiveMedia(&state);SaveState(&state);}
            }
            py+=36;
            if(state.count>0){
                float dw=(float)bw/state.count;
                for(int i=0;i<state.count;i++){
                    Color dc=i==state.currentIndex?C_ACCENT:
                             state.items[i].skip?C_SKIP:
                             state.items[i].isProcessed?C_GREEN:C_BTN;
                    DrawRectangle(px+(int)(i*dw),py,(int)Clamp(dw-1,1,20),5,dc);
                }
            }
            py+=12;HLine(px-4,py,bw+8);py+=10;
            if(Btn((Rectangle){px,py,bw,26},state.showLogWindow?"Hide Log":"Show Log",C_BTN))
                state.showLogWindow=!state.showLogWindow;
        }

        {
            int bpy=(int)winH-52;
            HLine(px-4,bpy-6,bw+8);
            Txt(TextFormat("%d files",state.count),px,bpy-20,FS_SM,C_DIM);
            if(!state.isProcessingBatch){
                if(BtnC((Rectangle){px,bpy,bw,40},"Process Batch",C_GREEN,C_BG))
                    {state.isProcessingBatch=true;state.batchIndex=0;state.logs.count=0;}
            } else {
                DrawRectangle(px,bpy,bw,40,C_BTN);
                DrawRectangle(px,bpy,(int)(bw*(state.count>0?(float)state.batchIndex/state.count:0)),40,C_GREEN);
                const char *pt=TextFormat("%d/%d",state.batchIndex,state.count);
                Txt(pt,px+(bw-TxtW(pt,FS_MD))/2,bpy+(40-FS_MD)/2,FS_MD,C_BG);
            }
        }

        if(state.showLogWindow){
            float lw=580,lh=360;
            Rectangle lr={winW/2-lw/2,winH/2-lh/2,lw,lh};
            DrawRectangleRec(lr,(Color){10,10,14,250});
            DrawRectangleLinesEx(lr,1,C_BORDER);
            Txt("LOG",(int)(lr.x+14),(int)(lr.y+12),FS_MD,C_DIM);
            if(!state.isProcessingBatch&&Btn((Rectangle){lr.x+lw-70,lr.y+8,58,24},"close",C_BTN))
                state.showLogWindow=false;
            BeginScissorMode((int)lr.x,(int)(lr.y+38),(int)lw,(int)(lh-46));
            int ly=(int)(lr.y+40);
            for(int i=state.logs.scrollOffset;i<state.logs.count;i++){
                Txt(state.logs.messages[i],(int)(lr.x+14),ly,FS_SM,state.logs.colors[i]);ly+=20;
            }
            EndScissorMode();
        }

        EndDrawing();
    }

    WorkerDestroy(&state.worker);
    if(G_FONT_OK)UnloadFont(G_FONT);
    SaveState(&state);
    CloseWindow();
    return 0;
}
