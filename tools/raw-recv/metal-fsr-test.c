/* macOS GPU regression fixture; generated pixels only, no console required. */
#define main viewer_main
#define SDL_GetPrefPath fsr_test_pref_path
#include "raw-view.c"
#undef main
#undef SDL_GetPrefPath
#include <assert.h>

char *SDLCALL fsr_test_pref_path(const char *org, const char *app)
{
    (void)org; (void)app;
    return SDL_strdup("/tmp/frame-tap-metal-fsr-test-");
}

static Uint32 pixel(SDL_Renderer *ren, int x, int y)
{
    SDL_RenderFlush(ren);
    int lw, lh;
    SDL_RenderGetLogicalSize(ren, &lw, &lh);
    SDL_RenderSetLogicalSize(ren, 0, 0);
    SDL_Rect r = {x, y, 1, 1};
    Uint32 p = 0;
    assert(SDL_RenderReadPixels(ren, &r, SDL_PIXELFORMAT_ARGB8888, &p, 4) == 0);
    SDL_RenderSetLogicalSize(ren, lw, lh);
    return p;
}

static void frame(SDL_Renderer *ren, int nv12, int color, int cached)
{
    /* Padded rows and odd dimensions catch texture pitch/chroma mistakes. */
    enum { W = 65, H = 37, P = 80, C = 40 };
    uint8_t y[P * H], u[C * 19], v[C * 19], uv[2 * C * 19];
    const uint8_t colors[][3] = {{16,128,128},{235,128,128},{63,102,240},
                                 {173,42,26},{32,240,118}};
    memset(y, colors[color][0], sizeof(y));
    memset(u, colors[color][1], sizeof(u));
    memset(v, colors[color][2], sizeof(v));
    for (size_t i = 0; i < sizeof(uv); i += 2) {
        uv[i] = colors[color][1]; uv[i + 1] = colors[color][2];
    }
    uint8_t *pd[] = {y, nv12 ? uv : u, nv12 ? NULL : v};
    const int pl[] = {P, nv12 ? 2 * C : C, C};
    SDL_RenderSetLogicalSize(ren, W, H);
    SDL_SetRenderDrawColor(ren,0,0,0,255);
    SDL_RenderClear(ren);
    if (cached) assert(clarity_draw(ren,NULL,NULL,0,0,g_fsr_sharpness/100.0f,0));
    else assert(up_draw(ren,pd,pl,W,H,UP_FSR,nv12));
    int kernel = 0;
    assert(clarity_status(&kernel,NULL) && kernel == 2);
    int ow,oh;
    SDL_GetRendererOutputSize(ren,&ow,&oh);
    Uint32 p = pixel(ren,ow/2,oh/2);
    int r=(p>>16)&255, g=(p>>8)&255, b=p&255;
    if (color==0) assert(r<=1 && g<=1 && b<=1);
    if (color==1) assert(r>=254 && g>=254 && b>=254);
    if (color==2) assert(r>245 && g<10 && b<10);
    if (color==3) assert(g>245 && r<10 && b<10);
    if (color==4) assert(b>245 && r<10 && g<10);
    SDL_RenderPresent(ren);
}

static void benchmark(SDL_Window *win, SDL_Renderer *ren)
{
    const int w=1920,h=1080;
    SDL_SetWindowSize(win,1280,720);
    SDL_PumpEvents(); SDL_Delay(200);
    int ow,oh; SDL_GetRendererOutputSize(ren,&ow,&oh);
    assert(ow==2560 && oh==1440);
    uint8_t *y=malloc(w*h), *u=malloc(w*h/4), *v=malloc(w*h/4);
    assert(y && u && v);
    for (int j=0;j<h;j++) for(int i=0;i<w;i++) y[j*w+i]=(uint8_t)(16+((i*7+j*11) % 220));
    memset(u,128,w*h/4); memset(v,128,w*h/4);
    uint8_t *pd[]={y,u,v}; int pl[]={w,w/2,w/2};
    SDL_Texture *tex=SDL_CreateTexture(ren,SDL_PIXELFORMAT_IYUV,SDL_TEXTUREACCESS_STREAMING,w,h);
    assert(tex);
    SDL_RenderSetLogicalSize(ren,w,h);
    for (int mode=0;mode<=1;mode++) {
        Uint64 start=0;
        for(int i=0;i<150;i++) {
            if(i==30) start=SDL_GetPerformanceCounter();
            SDL_UpdateYUVTexture(tex,NULL,y,w,u,w/2,v,w/2);
            SDL_RenderClear(ren);
            if (!up_draw(ren,pd,pl,w,h,mode?UP_FSR:UP_OFF,0)) SDL_RenderCopy(ren,tex,NULL,NULL);
            if(mode) { int k; assert(clarity_status(&k,NULL) && k==2); }
            SDL_RenderPresent(ren);
        }
        printf("%s 2560x1440: %.2f ms/frame (upload + filter + present, 120 frames)\n",
            mode?"FSR":"Off",(SDL_GetPerformanceCounter()-start)*1000.0/SDL_GetPerformanceFrequency()/120);
    }
    SDL_DestroyTexture(tex); free(y); free(u); free(v);
}

int main(int argc,char **argv)
{
    assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)==0);
    SDL_SetHint(SDL_HINT_RENDER_DRIVER,"metal");
    SDL_Window *win=SDL_CreateWindow("Metal FSR regression",SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,901,701,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
    assert(win);
    SDL_Renderer *ren=SDL_CreateRenderer(win,-1,SDL_RENDERER_ACCELERATED);
    assert(ren && clarity_init(ren));
    g_menu=0; g_upscaler=UP_FSR;
    for(int fs=0;fs<2;fs++) {
        assert(SDL_SetWindowFullscreen(win,fs?SDL_WINDOW_FULLSCREEN_DESKTOP:0)==0);
        SDL_PumpEvents(); SDL_Delay(200);
        for(int i=0;i<12;i++) {
            g_fsr_sharpness=(int[]){40,70,100}[i%3];
            frame(ren,i%2,i%5,0);
            frame(ren,i%2,i%5,1);
        }
    }
    assert(SDL_SetWindowFullscreen(win,0)==0);
    SDL_SetWindowSize(win,903,703); SDL_PumpEvents(); SDL_Delay(200);
    frame(ren,1,3,0);
    /* SDL HUD and drawer must render after our encoder state changes. */
    SDL_RenderClear(ren);
    assert(clarity_draw(ren,NULL,NULL,0,0,g_fsr_sharpness/100.0f,0));
    SDL_RenderSetLogicalSize(ren,1280,720);
    g_hud=HUD_FULL; g_hud_toast=0;
    hud_sec hs={.fps=60,.w=65,.h=37}; hud_second(&hs);
    hud_draw(ren,1280,720,"USB","");
    g_ui_open=1; ui_overlay(ren,1280,720);
    float sx,sy; SDL_RenderGetScale(ren,&sx,&sy);
    printf("UI scale %.3f %.3f, width %.1f, hits %d\n",sx,sy,g_ui_w,g_ui_nhits);
    int ow,oh; SDL_GetRendererOutputSize(ren,&ow,&oh);
    Uint32 panel = pixel(ren,ow-3,oh/2);
    assert(((panel>>16)&255)<60 && ((panel>>8)&255)<60 && (panel&255)<70);
    SDL_RenderFlush(ren); SDL_RenderSetLogicalSize(ren,0,0);
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,ow,oh,32,SDL_PIXELFORMAT_ARGB8888);
    assert(s && SDL_RenderReadPixels(ren,NULL,s->format->format,s->pixels,s->pitch)==0);
    assert(SDL_SaveBMP(s,"/tmp/frame-tap-metal-fsr-ui.bmp")==0);
    SDL_FreeSurface(s); SDL_RenderPresent(ren); g_ui_open=0;
    char label[120]; up_hud_text(label,sizeof(label)); assert(strstr(label,"EASU"));
    assert(text_width(label,16,1)+text_width("Upscaling",16,0)<316);
    /* Exercise the actual physical-key dispatch and saved selection. */
    SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);
    SDL_Event e={.type=SDL_KEYDOWN}; e.key.keysym.sym=SDLK_F9;
    e.key.keysym.scancode=SDL_SCANCODE_F9;
    g_fsr_sharpness=100; SDL_PushEvent(&e); app_events(win,ren,NULL);
    assert(g_fsr_sharpness==40 && g_upscaler==UP_FSR);
    prefs_load(); assert(g_fsr_sharpness==40);
    e.key.keysym.sym=SDLK_UNKNOWN; e.key.keysym.scancode=SDL_SCANCODE_U;
    SDL_PushEvent(&e); app_events(win,ren,NULL); assert(g_upscaler==UP_OFF);
    uint8_t dummy=0; uint8_t *pd[]={&dummy,&dummy,&dummy}; int pl[]={1,1,1};
    assert(!up_draw(ren,pd,pl,1,1,UP_OFF,0));
    SDL_SetWindowSize(win,64,36); SDL_PumpEvents(); SDL_Delay(100);
    assert(!up_draw(ren,pd,pl,1920,1080,UP_FSR,0));
    if(argc>1 && !strcmp(argv[1],"--benchmark")) benchmark(win,ren);
    up_reset(); SDL_DestroyRenderer(ren);
    SDL_SetHint(SDL_HINT_RENDER_DRIVER,"software");
    ren=SDL_CreateRenderer(win,-1,SDL_RENDERER_SOFTWARE); assert(ren);
    SDL_RendererInfo info; assert(SDL_GetRendererInfo(ren,&info)==0);
    assert(!strcmp(info.name,"software"));
    assert(!clarity_init(ren)); /* renderer fallback must be safe */
    up_reset(); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    puts("Metal FSR: NV12/IYUV, colors, cached redraw, resize, fullscreen, controls and fallback passed");
    return 0;
}
