#define main viewer_main
#define SDL_GetPrefPath hud_test_pref_path
#define SDL_RenderPresent hud_test_present
#include "raw-view.c"
#undef main
#undef SDL_GetPrefPath
#undef SDL_RenderPresent
#include <assert.h>
extern DECLSPEC void SDLCALL SDL_RenderPresent(SDL_Renderer *ren);
static int capture_menu;

void SDLCALL hud_test_present(SDL_Renderer *ren)
{
    if (capture_menu) {
        int w, h, lw, lh;
        SDL_GetRendererOutputSize(ren, &w, &h);
        SDL_RenderGetLogicalSize(ren, &lw, &lh);
        SDL_RenderFlush(ren);
        SDL_RenderSetLogicalSize(ren, 0, 0);
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_Rect rect = {0, 0, w, h};
        assert(s && SDL_RenderReadPixels(ren, &rect, s->format->format, s->pixels, s->pitch) == 0);
        Uint8 r, g, b;
        SDL_GetRGB(((Uint32 *)((char *)s->pixels + (h - 1) * s->pitch))[w - 1], s->format, &r, &g, &b);
        assert(r == 18 && g == 20 && b == 24);
        SDL_FreeSurface(s);
        SDL_RenderSetLogicalSize(ren, lw, lh);
        puts("Waiting menu clears bottom letterbox pixels OK");
    }
    SDL_RenderPresent(ren);
}

char *SDLCALL hud_test_pref_path(const char *org, const char *app)
{
    (void)org; (void)app;
    return SDL_strdup("/tmp/frame-tap-hud-test-");
}

static void check_picture(SDL_Window *win, SDL_Renderer *ren, int fullscreen)
{
    assert(SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) == 0);
    SDL_PumpEvents();
    SDL_Delay(100);
    assert(SDL_RenderSetLogicalSize(ren, 1920, 1080) == 0);
    uint8_t *y = malloc(1920 * 1080), *u = malloc(960 * 540), *v = malloc(960 * 540);
    memset(y, 100, 1920 * 1080); memset(u, 128, 960 * 540); memset(v, 128, 960 * 540);
    const uint8_t *planes[] = { y, u, v };
    const int pitches[] = { 1920, 960, 960 };
    int w, h;
    SDL_GetRendererOutputSize(ren, &w, &h);
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    assert(s);
    for (int mode = HUD_FPS; mode <= HUD_FULL; mode++) {
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        assert(clarity_draw(ren, planes, pitches, 1920, 1080, .45f));
        g_hud = mode; g_hud_toast = 0;
        hud_sec hs = { .fps = 40, .w = 1920, .h = 1080 };
        hud_second(&hs);
        assert(strstr(video_filter_label(), "RCAS 45%"));
        assert(strstr(video_filter_label(), fmin(w / 1920.0, h / 1080.0) > 1.001 ? "FSR 1 EASU" : "Bilinear"));
        hud_draw(ren, 1920, 1080, "USB", "", video_filter_label());
        SDL_RenderFlush(ren);
        SDL_RenderSetLogicalSize(ren, 0, 0);
        SDL_Rect read_rect = {0, 0, w, h};
        SDL_FillRect(s, NULL, 0);
        assert(SDL_RenderReadPixels(ren, &read_rect, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0);
        // Flat fields must remain neutral (including EASU's zero-gradient case).
        Uint8 flat_r, flat_g, flat_b;
        SDL_GetRGB(((Uint32 *)((char *)s->pixels + (h / 2) * s->pitch))[w * 9 / 10],
                   s->format, &flat_r, &flat_g, &flat_b);
        assert(flat_r >= 96 && flat_r <= 100);
        assert(flat_g == flat_r && flat_b == flat_r);
        // Every overlay must be inside the top-left of the letterboxed image.
        const double scale = fmin(w / 1280.0, h / 720.0);
        const int x0 = (int)((w - 1280 * scale) / 2);
        const int y0 = (int)((h - 720 * scale) / 2);
        int bright = 0;
        for (int yy = y0; yy < y0 + (int)(50 * scale); yy++)
            for (int xx = x0; xx < x0 + (int)(300 * scale); xx++) {
                Uint8 r, g, b;
                Uint32 pixel = ((Uint32 *)((char *)s->pixels + yy * s->pitch))[xx];
                SDL_GetRGB(pixel, s->format, &r, &g, &b);
                bright += g > 150 && r > 100;
            }
        printf("HUD %s fullscreen=%d output=%dx%d visible_pixels=%d\n", hud_mode_name[mode], fullscreen, w, h, bright);
        SDL_SaveBMP(s, "/tmp/frame-tap-hud-check.bmp");
        assert(bright > 30);
        SDL_RenderPresent(ren);
        SDL_RenderSetLogicalSize(ren, 1920, 1080);
    }
    SDL_FreeSurface(s); free(y); free(u); free(v);
}

static void check_easu_updates(SDL_Window *win, SDL_Renderer *ren)
{
    uint8_t y[64 * 36], u[32 * 18], v[32 * 18];
    memset(u,128,sizeof(u)); memset(v,128,sizeof(v));
    const uint8_t *planes[] = {y,u,v};
    const int pitches[] = {64,32,32};
    assert(SDL_SetWindowFullscreen(win,0) == 0);
    for (int i = 0; i < 10; i++) {
        // Odd viewport sizes and more than four updates exercise cache invalidation
        // and resource rotation. A redraw without a new frame must stay identical.
        SDL_SetWindowSize(win,901+i,701+i);
        SDL_PumpEvents();
        memset(y,i & 1 ? 235 : 16,sizeof(y));
        for (int redraw = 0; redraw < 2; redraw++) {
            SDL_RenderSetLogicalSize(ren,64,36);
            SDL_SetRenderDrawColor(ren,0,0,0,255);
            SDL_RenderClear(ren);
            assert(clarity_draw(ren,redraw ? NULL : planes,redraw ? NULL : pitches,
                                64,36,i % 3 == 0 ? 0 : 1));
            int mode;
            assert(clarity_status(&mode,NULL) && mode == 2);
            SDL_RenderFlush(ren);
            SDL_RenderSetLogicalSize(ren,0,0);
            int w,h;
            SDL_GetRendererOutputSize(ren,&w,&h);
            SDL_Rect rect = {w/2,h/2,1,1};
            Uint32 pixel;
            assert(SDL_RenderReadPixels(ren,&rect,SDL_PIXELFORMAT_ARGB8888,&pixel,4) == 0);
            Uint8 r = (pixel >> 16) & 255, g = (pixel >> 8) & 255, b = pixel & 255;
            assert(r == g && g == b);
            assert((i & 1) ? r >= 254 : r <= 1);
            SDL_RenderPresent(ren);
        }
    }
    const uint8_t colors[3][3] = {{63,102,240},{173,42,26},{32,240,118}};
    for (int i = 0; i < 3; i++) {
        memset(y,colors[i][0],sizeof(y));
        memset(u,colors[i][1],sizeof(u));
        memset(v,colors[i][2],sizeof(v));
        SDL_RenderSetLogicalSize(ren,64,36);
        SDL_RenderClear(ren);
        assert(clarity_draw(ren,planes,pitches,64,36,1));
        SDL_RenderFlush(ren);
        SDL_RenderSetLogicalSize(ren,0,0);
        int w,h;
        SDL_GetRendererOutputSize(ren,&w,&h);
        SDL_Rect rect = {w/2,h/2,1,1};
        Uint32 pixel;
        assert(SDL_RenderReadPixels(ren,&rect,SDL_PIXELFORMAT_ARGB8888,&pixel,4) == 0);
        Uint8 channels[] = {(pixel >> 16) & 255,(pixel >> 8) & 255,pixel & 255};
        for (int c = 0; c < 3; c++) assert(c == i ? channels[c] >= 250 : channels[c] <= 3);
        SDL_RenderPresent(ren);
    }
    puts("EASU RGB preparation preserves BT.709 red/green/blue OK");
    puts("EASU fresh/cached frames, resize, texture rotation, strength 0/100 OK");
}

static void benchmark_clarity(SDL_Window *win, SDL_Renderer *ren)
{
    assert(SDL_SetWindowFullscreen(win,0) == 0);
    SDL_SetWindowSize(win,1280,720);
    SDL_PumpEvents();
    SDL_Delay(200);
    int w,h;
    SDL_GetRendererOutputSize(ren,&w,&h);
    assert(w > 1920 && h > 1080);
    uint8_t *y = malloc(1920*1080), *uv = malloc(960*540);
    assert(y && uv);
    for (int row = 0; row < 1080; row++) for (int col = 0; col < 1920; col++)
        y[row*1920+col] = 16 + ((row*13 ^ col*7) % 220);
    memset(uv,128,960*540);
    const uint8_t *planes[] = {y,uv,uv};
    const int pitches[] = {1920,960,960};
    SDL_RenderSetLogicalSize(ren,1920,1080);
    Uint64 started = 0;
    for (int i = 0; i < 210; i++) {
        SDL_PumpEvents();
        if (i == 30) started = SDL_GetPerformanceCounter();
        SDL_RenderClear(ren);
        assert(clarity_draw(ren,planes,pitches,1920,1080,1));
        int mode, now_w, now_h;
        assert(clarity_status(&mode,NULL) && mode == 2);
        SDL_GetRendererOutputSize(ren,&now_w,&now_h);
        assert(now_w == w && now_h == h);
        SDL_RenderPresent(ren);
    }
    double ms = (SDL_GetPerformanceCounter()-started)*1000.0/SDL_GetPerformanceFrequency()/180;
    printf("Clarity 1080p -> %dx%d: upload + filter + present %.2f ms/frame (180 frames, 30 warmup)\n",w,h,ms);
    free(y); free(uv);
}

int main(int argc, char **argv)
{
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) == 0);
    SDL_Window *win = SDL_CreateWindow("HUD regression", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, 900, 700, VIDEO_WINDOW_FLAGS);
    assert(win);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    assert(ren);
    if (argc > 1 && !strcmp(argv[1],"--benchmark")) {
        benchmark_clarity(win,ren);
        clarity_close();
        SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
        return 0;
    }
    check_picture(win, ren, 0);
    check_picture(win, ren, 1);
    clarity_set_easu(0);
    assert(clarity_draw(ren, NULL, NULL, 0, 0, .45f));
    assert(strstr(video_filter_label(), "Catmull-Rom"));
    SDL_RenderPresent(ren);
    clarity_set_easu(1);
    check_easu_updates(win,ren);
    capture_menu = 1;
    menu_draw(ren);
    capture_menu = 0;
    g_clarity = 0;
    assert(strstr(video_filter_label(), "enhancement off"));
    g_clarity = 1;
    clarity_close();
    assert(strstr(video_filter_label(), "fallback"));
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    g_hud = HUD_OFF;
    SDL_Event e = {0}; e.type = SDL_KEYDOWN;
    e.key.keysym.scancode = SDL_SCANCODE_O;
    e.key.keysym.sym = 0x0449; // Russian-layout key at the physical O position.
    assert(SDL_PushEvent(&e) == 1);
    app_events(win, ren, NULL);
    assert(g_hud == HUD_FPS);
    g_sharpness = .4f;
    e.key.keysym.scancode = SDL_SCANCODE_F9;
    e.key.keysym.sym = SDLK_F9;
    const float strengths[] = {.7f, 1.0f, .4f};
    for (int i = 0; i < 3; i++) {
        assert(SDL_PushEvent(&e) == 1);
        app_events(win, ren, NULL);
        assert(g_sharpness == strengths[i] && g_clarity);
    }
    puts("F9 sharpness cycle 40/70/100 OK");
    clarity_close();
    SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    puts("HUD regression OK");
    return 0;
}
