#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include "clarity.h"
#include "clarity_shader.h"
#include <simd/simd.h>

static id<MTLDevice> device;
static id<MTLRenderPipelineState> pipeline;
static id<MTLTexture> textures[4][3];
static SDL_Renderer *owner;
static int tex_width, tex_height, slot = -1, failed;
static int active, active_cubic;
static float active_strength;

int clarity_status(int *cubic, float *strength)
{
    if (cubic) *cubic = active_cubic;
    if (strength) *strength = active_strength;
    return active;
}

void clarity_close(void)
{
    for (int i = 0; i < 4; i++) for (int j = 0; j < 3; j++) textures[i][j] = nil;
    pipeline = nil; device = nil; owner = NULL;
    tex_width = tex_height = 0; slot = -1; failed = 0;
    active = 0;
}

static int setup(SDL_Renderer *ren)
{
    if (owner == ren) return pipeline != nil;
    clarity_close();
    owner = ren;
    CAMetalLayer *layer = (__bridge CAMetalLayer *)SDL_RenderGetMetalLayer(ren);
    if (!layer) return 0;
    device = layer.device;
    NSString *source = [[NSString alloc] initWithBytes:clarity_metal
        length:clarity_metal_len encoding:NSUTF8StringEncoding];
    NSError *error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
    MTLRenderPipelineDescriptor *desc = [MTLRenderPipelineDescriptor new];
    desc.vertexFunction = [library newFunctionWithName:@"clarity_vertex"];
    desc.fragmentFunction = [library newFunctionWithName:@"clarity_fragment"];
    desc.colorAttachments[0].pixelFormat = layer.pixelFormat;
    if (library) pipeline = [device newRenderPipelineStateWithDescriptor:desc error:&error];
    if (!pipeline) {
        fprintf(stderr, "clarity: Metal unavailable: %s\n", error.localizedDescription.UTF8String ?: "no shader");
        return 0;
    }
    fprintf(stderr, "clarity: Metal RCAS + Catmull-Rom, one render pass\n");
    return 1;
}

int clarity_init(SDL_Renderer *ren) { @autoreleasepool { return setup(ren); } }

int clarity_draw(SDL_Renderer *ren, const uint8_t *const planes[3],
                 const int pitches[3], int width, int height, float strength)
{ @autoreleasepool {
    active = 0;
    if (!setup(ren) || failed) return 0;
    int ow, oh;
    if (SDL_GetRendererOutputSize(ren, &ow, &oh) < 0 || ow <= 0 || oh <= 0) return 0;
    if (!planes && slot < 0) return 0;
    // Flush SDL's clear and acquire a drawable before reusing upload storage.
    id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)SDL_RenderGetMetalCommandEncoder(ren);
    if (!encoder) return 0;
    if (planes) {
        if (width != tex_width || height != tex_height) {
            for (int i = 0; i < 4; i++) for (int j = 0; j < 3; j++) {
                MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm
                    width:j ? (width + 1) / 2 : width height:j ? (height + 1) / 2 : height mipmapped:NO];
                desc.storageMode = MTLStorageModeShared;
                desc.usage = MTLTextureUsageShaderRead;
                textures[i][j] = [device newTextureWithDescriptor:desc];
                if (!textures[i][j]) { failed = 1; return 0; }
            }
            tex_width = width; tex_height = height; slot = -1;
        }
        // Four upload sets cover Metal's maximum three in-flight drawables.
        slot = (slot + 1) % 4;
        for (int j = 0; j < 3; j++) {
            id<MTLTexture> t = textures[slot][j];
            [t replaceRegion:MTLRegionMake2D(0, 0, t.width, t.height)
                mipmapLevel:0 withBytes:planes[j] bytesPerRow:pitches[j]];
        }
    }
    double scale = fmin((double)ow / tex_width, (double)oh / tex_height);
    double vw = tex_width * scale, vh = tex_height * scale;
    [encoder setViewport:(MTLViewport){(ow - vw) / 2, (oh - vh) / 2, vw, vh, 0, 1}];
    [encoder setScissorRect:(MTLScissorRect){0, 0, (NSUInteger)ow, (NSUInteger)oh}];
    [encoder setRenderPipelineState:pipeline];
    struct { vector_float2 size; float strength; uint32_t cubic; } params = {
        {(float)tex_width, (float)tex_height}, strength, scale > 1.001
    };
    [encoder setFragmentBytes:&params length:sizeof(params) atIndex:0];
    for (int j = 0; j < 3; j++) [encoder setFragmentTexture:textures[slot][j] atIndex:j];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    active = 1; active_cubic = params.cubic; active_strength = strength;
    return 1;
}}
