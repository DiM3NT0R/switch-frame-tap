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
        assert(strstr(video_filter_label(), fmin(w / 1920.0, h / 1080.0) > 1.001 ? "Catmull-Rom" : "Bilinear"));
        hud_draw(ren, 1920, 1080, "USB", "", video_filter_label());
        SDL_RenderFlush(ren);
        SDL_RenderSetLogicalSize(ren, 0, 0);
        SDL_Rect read_rect = {0, 0, w, h};
        SDL_FillRect(s, NULL, 0);
        assert(SDL_RenderReadPixels(ren, &read_rect, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0);
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

int main(void)
{
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) == 0);
    SDL_Window *win = SDL_CreateWindow("HUD regression", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, 900, 700, VIDEO_WINDOW_FLAGS);
    assert(win);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    assert(ren);
    check_picture(win, ren, 0);
    check_picture(win, ren, 1);
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
    clarity_close();
    SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    puts("HUD regression OK");
    return 0;
}
