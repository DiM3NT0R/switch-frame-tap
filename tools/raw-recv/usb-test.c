#define main viewer_main
#define libusb_bulk_transfer test_bulk
#define libusb_clear_halt test_clear
#include "raw-view.c"
#undef main
#undef libusb_bulk_transfer
#undef libusb_clear_halt
#include <assert.h>

static uint8_t input[2048];
static size_t input_len, input_pos;
static int pipes, clears, calls;
static int fragment, idle, transient_io;

int LIBUSB_CALL test_clear(libusb_device_handle *handle, unsigned char endpoint)
{
    (void)handle; assert(endpoint == SFT_EP_IN); clears++;
    return 0;
}

int LIBUSB_CALL test_bulk(libusb_device_handle *handle, unsigned char endpoint,
                         unsigned char *data, int length, int *transferred, unsigned int timeout)
{
    (void)handle; (void)timeout;
    assert(endpoint == SFT_EP_IN);
    assert(length >= 1024 && length % 1024 == 0);
    calls++; *transferred = 0;
    if (transient_io > 0) { transient_io--; return LIBUSB_ERROR_IO; }
    if (idle && input_pos == 16) { idle = 0; return LIBUSB_ERROR_TIMEOUT; }
    if (pipes > 0) { pipes--; return LIBUSB_ERROR_PIPE; }
    if (input_pos == input_len) return LIBUSB_ERROR_NO_DEVICE;
    size_t n = input_len - input_pos;
    if (n > (size_t)length) n = (size_t)length;
    if (fragment && n > (size_t)fragment) n = (size_t)fragment;
    memcpy(data, input + input_pos, n); input_pos += n;
    *transferred = (int)n;
    return 0;
}

static void reset(void)
{
    g_usb_readpos = g_usb_readlen = 0; g_usb_stall_retry = 0;
    g_app = 1; g_lost = g_quit = g_rd_stop = 0;
    input_pos = input_len = 0; pipes = clears = calls = 0;
    fragment = idle = transient_io = 0;
}

int main(void)
{
    assert(SDL_Init(SDL_INIT_TIMER) == 0);
    reset();
    for (size_t i = 0; i < sizeof(input); i++) input[i] = (uint8_t)i;
    input_len = sizeof(input);
    uint8_t data[1500];
    assert(read_exact(data, 32, 100) == 32);
    assert(read_exact(data + 32, sizeof(data) - 32, 100) == (int)sizeof(data) - 32);
    assert(memcmp(data, input, sizeof(data)) == 0 && calls == 1);
    reset(); input_len = 32; fragment = 16; idle = 1;
    assert(read_exact(data, 32, 100) == 32 && !g_lost && calls == 3);
    reset(); input_len = 32; transient_io = 2;
    assert(read_exact(data, 32, 100) == 32 && !g_lost);
#ifdef SFT_H264
    assert(!default_low_delay(0,0) && !default_low_delay(1,1));
#ifdef __APPLE__
    assert(default_low_delay(1,0));
#endif
    h264_t low = {0};
    assert(h264_open(&low,1,2,0) == 0);
    assert(low.c->thread_count == 1 && (low.c->flags & AV_CODEC_FLAG_LOW_DELAY));
    assert(!(low.c->active_thread_type & FF_THREAD_FRAME));
    h264_close(&low);
    assert(h264_open(&low,0,2,0) == 0);
    assert(low.c->thread_count == 2 && !(low.c->flags & AV_CODEC_FLAG_LOW_DELAY));
    h264_close(&low);
    assert(h264_open(&low,-1,2,0) == 0);
    assert(low.c->thread_count == 2 && !(low.c->flags & AV_CODEC_FLAG_LOW_DELAY));
    h264_close(&low);
    puts("Low-delay decoder and explicit frame-thread override OK");
    h264_t decoder = { .hwfmt = AV_PIX_FMT_VIDEOTOOLBOX, .videotoolbox = 1 };
    AVCodecContext context = { .opaque = &decoder };
    const enum AVPixelFormat formats[] = {AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE};
    assert(h264_get_hw_format(&context, formats) == AV_PIX_FMT_YUV420P && !decoder.videotoolbox);
#endif
    reset(); input_len = 32; pipes = 1;
    assert(read_exact(data, 32, 100) == 32 && clears == 1 && !g_lost);
    reset(); pipes = 2;
    assert(read_exact(data, 32, 100) < 0 && clears == 1 && calls == 2 && g_lost);

    reset();
    memset(input, 0xaa, 17); input_len = 17;
    sft_hdr_t hdr = { .magic = SFT_MAGIC, .version = 2, .flags = SFT_FLAG_H264,
        .width = 1280, .height = 720, .length = 12, .kind = 7 };
    memcpy(input + input_len, &hdr, sizeof(hdr)); input_len += sizeof(hdr);
    memset(input + input_len, 0x55, hdr.length); input_len += hdr.length;
    g_qm = SDL_CreateMutex(); g_q_put = SDL_CreateCond(); g_q_get = SDL_CreateCond();
    assert(g_qm && g_q_put && g_q_get);
    reader_main(NULL);
    assert(g_qn == 1 && g_q[0].hdr.kind == 7 && g_q[0].data[0] == 0x55);
    SDL_Quit();
    puts("USB buffering, resync, and bounded stall recovery OK");
    return 0;
}
