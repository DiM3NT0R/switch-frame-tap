#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include "clarity.h"
#include "clarity_shader.h"
#include <simd/simd.h>

static id<MTLDevice> device;
static id<MTLRenderPipelineState> pipeline;
static id<MTLRenderPipelineState> rcas_pipeline;
static id<MTLComputePipelineState> easu_pipeline;
static id<MTLComputePipelineState> rgb_pipeline;
static id<MTLCommandQueue> compute_queue;
static id<MTLTexture> textures[4][3];
static id<MTLTexture> upscaled[4];
static id<MTLTexture> source_rgb;
static SDL_Renderer *owner;
static int tex_width, tex_height, slot = -1, failed;
static int tex_nv12;
static int output_width, output_height, output_slot = -1, output_source = -1;
static double compute_gpu_ms, compute_wait_ms;
static unsigned compute_samples;
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
    for (int i = 0; i < 4; i++) upscaled[i] = nil;
    rcas_pipeline = nil; easu_pipeline = nil; rgb_pipeline = nil; compute_queue = nil;
    source_rgb = nil;
    pipeline = nil; device = nil; owner = NULL;
    tex_width = tex_height = 0; slot = -1; failed = 0;
    output_width = output_height = 0; output_slot = output_source = -1;
    compute_gpu_ms = compute_wait_ms = 0; compute_samples = 0;
    active = 0;
}

static int setup(SDL_Renderer *ren)
{
    if (owner == ren) return pipeline != nil;
    clarity_close();
    owner = ren;
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(ren, &info) != 0 || !info.name || strcmp(info.name, "metal") != 0)
        return 0;
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
    desc.fragmentFunction = [library newFunctionWithName:@"clarity_rcas"];
    rcas_pipeline = [device newRenderPipelineStateWithDescriptor:desc error:&error];
    id<MTLFunction> easu = [library newFunctionWithName:@"clarity_easu"];
    if (easu) easu_pipeline = [device newComputePipelineStateWithFunction:easu error:&error];
    id<MTLFunction> rgb = [library newFunctionWithName:@"clarity_rgb"];
    if (rgb) rgb_pipeline = [device newComputePipelineStateWithFunction:rgb error:&error];
    compute_queue = [device newCommandQueue];
    if (!rcas_pipeline || !easu_pipeline || !rgb_pipeline || !compute_queue)
        fprintf(stderr, "clarity: EASU unavailable, using cubic: %s\n", error.localizedDescription.UTF8String ?: "no compute queue");
    fprintf(stderr, "clarity: Metal EASU + RCAS, two passes; cubic fallback available\n");
    return 1;
}

int clarity_init(SDL_Renderer *ren) { @autoreleasepool { return setup(ren); } }

int clarity_draw(SDL_Renderer *ren, const uint8_t *const planes[3],
                 const int pitches[3], int width, int height, float strength, int nv12)
{ @autoreleasepool {
    active = 0;
    if (!setup(ren) || failed) return 0;
    int ow, oh;
    if (SDL_GetRendererOutputSize(ren, &ow, &oh) < 0 || ow <= 0 || oh <= 0) return 0;
    if (!planes && slot < 0) return 0;
    if (planes && (width <= 0 || height <= 0 || !pitches || !planes[0] || !planes[1] ||
        (!nv12 && !planes[2]) || pitches[0] < width ||
        pitches[1] < ((width + 1) / 2) * (nv12 ? 2 : 1) ||
        (!nv12 && pitches[2] < (width + 1) / 2))) return 0;
    // Flush SDL's clear and acquire a drawable before reusing upload storage.
    id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)SDL_RenderGetMetalCommandEncoder(ren);
    if (!encoder) return 0;
    if (planes) {
        output_source = -1;
        if (width != tex_width || height != tex_height || nv12 != tex_nv12) {
            for (int i = 0; i < 4; i++) for (int j = 0; j < 3; j++) {
                MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:(nv12 && j == 1 ? MTLPixelFormatRG8Unorm : MTLPixelFormatR8Unorm)
                    width:j ? (width + 1) / 2 : width height:j ? (height + 1) / 2 : height mipmapped:NO];
                desc.storageMode = MTLStorageModeShared;
                desc.usage = MTLTextureUsageShaderRead;
                textures[i][j] = [device newTextureWithDescriptor:desc];
                if (!textures[i][j]) { failed = 1; return 0; }
            }
            tex_width = width; tex_height = height; tex_nv12 = nv12; slot = -1;
        }
        // Four upload sets cover Metal's maximum three in-flight drawables.
        slot = (slot + 1) % 4;
        for (int j = 0; j < 3; j++) {
            if (nv12 && j == 2) continue;
            id<MTLTexture> t = textures[slot][j];
            [t replaceRegion:MTLRegionMake2D(0, 0, t.width, t.height)
                mipmapLevel:0 withBytes:planes[j] bytesPerRow:pitches[j]];
        }
    }
    double scale = fmin((double)ow / tex_width, (double)oh / tex_height);
    int vw = MAX(1, (int)lround(tex_width * scale)), vh = MAX(1, (int)lround(tex_height * scale));
    [encoder setViewport:(MTLViewport){(ow - vw) / 2, (oh - vh) / 2, (double)vw, (double)vh, 0, 1}];
    [encoder setScissorRect:(MTLScissorRect){0, 0, (NSUInteger)ow, (NSUInteger)oh}];
    struct { vector_float2 size; float strength; uint32_t cubic;
             vector_float2 output_size; uint32_t nv12; } params = {
        {(float)tex_width, (float)tex_height}, strength, scale > 1.001,
        {(float)vw, (float)vh}, tex_nv12
    };
    int two_pass = params.cubic && easu_pipeline && rgb_pipeline && rcas_pipeline && compute_queue;
    if (two_pass) {
        if (output_width != vw || output_height != vh) {
            for (int i = 0; i < 4; i++) upscaled[i] = nil;
            output_width = vw; output_height = vh;
            output_slot = output_source = -1;
        }
        if (output_source != slot) {
            output_slot = (output_slot + 1) % 4;
            if (!upscaled[output_slot]) {
                MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                    width:vw height:vh mipmapped:NO];
                desc.storageMode = MTLStorageModePrivate;
                desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
                upscaled[output_slot] = [device newTextureWithDescriptor:desc];
            }
            if (source_rgb.width != (NSUInteger)tex_width || source_rgb.height != (NSUInteger)tex_height) {
                MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
                    width:tex_width height:tex_height mipmapped:NO];
                desc.storageMode = MTLStorageModePrivate;
                desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
                source_rgb = [device newTextureWithDescriptor:desc];
            }
            id<MTLCommandBuffer> command = [compute_queue commandBuffer];
            id<MTLComputeCommandEncoder> conversion = upscaled[output_slot] && source_rgb ? [command computeCommandEncoder] : nil;
            if (conversion) {
                [conversion setComputePipelineState:rgb_pipeline];
                [conversion setBytes:&params length:sizeof(params) atIndex:0];
                for (int j = 0; j < 3; j++) [conversion setTexture:textures[slot][j] atIndex:j];
                [conversion setTexture:source_rgb atIndex:3];
                NSUInteger tw = rgb_pipeline.threadExecutionWidth;
                NSUInteger th = MIN((NSUInteger)8, rgb_pipeline.maxTotalThreadsPerThreadgroup / tw);
                [conversion dispatchThreads:MTLSizeMake(tex_width,tex_height,1) threadsPerThreadgroup:MTLSizeMake(tw,th,1)];
                // Tracked texture hazards order conversion before EASU in this buffer.
                [conversion endEncoding];
            }
            id<MTLComputeCommandEncoder> compute = conversion ? [command computeCommandEncoder] : nil;
            if (compute) {
                [compute setComputePipelineState:easu_pipeline];
                [compute setBytes:&params length:sizeof(params) atIndex:0];
                [compute setTexture:source_rgb atIndex:0];
                [compute setTexture:upscaled[output_slot] atIndex:1];
                NSUInteger tw = easu_pipeline.threadExecutionWidth;
                NSUInteger th = MIN((NSUInteger)8, easu_pipeline.maxTotalThreadsPerThreadgroup / tw);
                [compute dispatchThreads:MTLSizeMake(vw, vh, 1) threadsPerThreadgroup:MTLSizeMake(tw, th, 1)];
                [compute endEncoding];
                Uint64 started = SDL_GetPerformanceCounter();
                [command commit];
                // SDL exposes its render encoder, not its command buffer. Complete
                // the producer before SDL submits the consumer on a different queue.
                // This waits for this frame only, never queues a future video frame.
                [command waitUntilCompleted];
                compute_wait_ms += (SDL_GetPerformanceCounter() - started) * 1000.0 / SDL_GetPerformanceFrequency();
                compute_gpu_ms += (command.GPUEndTime - command.GPUStartTime) * 1000.0;
                if (++compute_samples == 120) {
                    fprintf(stderr, "clarity: RGB + EASU GPU %.2f ms, submit/wait %.2f ms (%dx%d, 120 frames)\n",
                        compute_gpu_ms / compute_samples, compute_wait_ms / compute_samples, vw, vh);
                    compute_gpu_ms = compute_wait_ms = 0; compute_samples = 0;
                }
            }
            if (!compute || command.status != MTLCommandBufferStatusCompleted) {
                fprintf(stderr, "clarity: EASU compute failed, using cubic: %s\n", command.error.localizedDescription.UTF8String ?: "allocation failed");
                easu_pipeline = nil;
                two_pass = 0;
            } else output_source = slot;
        }
    }
    [encoder setRenderPipelineState:two_pass ? rcas_pipeline : pipeline];
    [encoder setFragmentBytes:&params length:sizeof(params) atIndex:0];
    if (two_pass) [encoder setFragmentTexture:upscaled[output_slot] atIndex:0];
    else for (int j = 0; j < 3; j++) [encoder setFragmentTexture:textures[slot][j] atIndex:j];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    active = 1; active_cubic = params.cubic ? (two_pass ? 2 : 1) : 0; active_strength = strength;
    return 1;
}}
