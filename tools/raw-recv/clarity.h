#ifndef SFT_CLARITY_H
#define SFT_CLARITY_H
#include <SDL2/SDL.h>
#include <stdint.h>
#ifdef __APPLE__
int clarity_init(SDL_Renderer *ren);
int clarity_draw(SDL_Renderer *ren, const uint8_t *const planes[3],
                 const int pitches[3], int width, int height, float strength);
void clarity_close(void);
int clarity_status(int *cubic, float *strength);
#else
static inline int clarity_init(SDL_Renderer *ren) { (void)ren; return 0; }
static inline int clarity_draw(SDL_Renderer *ren, const uint8_t *const p[3],
                               const int s[3], int w, int h, float strength)
{ (void)ren; (void)p; (void)s; (void)w; (void)h; (void)strength; return 0; }
static inline void clarity_close(void) {}
static inline int clarity_status(int *cubic, float *strength)
{ (void)cubic; (void)strength; return 0; }
#endif
#endif
