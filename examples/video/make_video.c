/* Generate the original 8-second motion test clip; no external artwork. */
#include <math.h>
#include <ogg/ogg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <theora/theoraenc.h>

enum
{
    WIDTH = 640,
    HEIGHT = 368,
    PICTURE_HEIGHT = 360,
    FPS = 30,
    FRAMES = FPS * 8
};
static unsigned char yplane[WIDTH * HEIGHT], uplane[WIDTH * HEIGHT / 4], vplane[WIDTH * HEIGHT / 4];
static void check(int ok, const char *message)
{
    if (!ok)
    {
        fprintf(stderr, "%s\n", message);
        exit(1);
    }
}
static void rect(int x, int y, int width, int height, int luma, int u, int v)
{
    for (int row = y; row < y + height && row < PICTURE_HEIGHT; row++)
        for (int col = x; col < x + width && col < WIDTH; col++)
        {
            if (row < 0 || col < 0)
                continue;
            yplane[row * WIDTH + col] = luma;
            uplane[(row / 2) * (WIDTH / 2) + col / 2] = u;
            vplane[(row / 2) * (WIDTH / 2) + col / 2] = v;
        }
}
static void digit(int number, int x)
{
    static const unsigned char segments[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
    const int bars[7][4] = {{4, 0, 24, 4},  {28, 4, 4, 24}, {28, 32, 4, 24}, {4, 56, 24, 4},
                            {0, 32, 4, 24}, {0, 4, 4, 24},  {4, 28, 24, 4}};
    for (int i = 0; i < 7; i++)
        if (segments[number] & (1 << i))
            rect(x + bars[i][0], 24 + bars[i][1], bars[i][2], bars[i][3], 210, 128, 128);
}
static void pages(FILE *file, ogg_stream_state *stream, int flush)
{
    ogg_page page;
    while (flush ? ogg_stream_flush(stream, &page) : ogg_stream_pageout(stream, &page))
    {
        check(fwrite(page.header, 1, page.header_len, file) == (size_t)page.header_len,
              "header write failed");
        check(fwrite(page.body, 1, page.body_len, file) == (size_t)page.body_len, "page write failed");
    }
}
int main(int argc, char **argv)
{
    check(argc == 2, "usage: make_video OUTPUT.ogv");
    FILE *out = fopen(argv[1], "wb");
    check(out != NULL, "cannot open output");
    th_info info;
    th_info_init(&info);
    info.frame_width = WIDTH;
    info.frame_height = HEIGHT;
    info.pic_width = WIDTH;
    info.pic_height = PICTURE_HEIGHT;
    info.fps_numerator = FPS;
    info.fps_denominator = 1;
    info.aspect_numerator = info.aspect_denominator = 1;
    info.colorspace = TH_CS_UNSPECIFIED;
    info.pixel_fmt = TH_PF_420;
    info.quality = 40;
    info.keyframe_granule_shift = 6;
    th_enc_ctx *encoder = th_encode_alloc(&info);
    check(encoder != NULL, "cannot allocate Theora encoder");
    th_info_clear(&info);
    th_comment comment;
    th_comment_init(&comment);
    th_comment_add_tag(&comment, "TITLE", "LoveSCUA motion test / 8 seconds / 30 fps");
    ogg_stream_state stream;
    check(ogg_stream_init(&stream, 0x4c4f5645) == 0, "cannot initialize Ogg");
    ogg_packet packet;
    int result;
    while ((result = th_encode_flushheader(encoder, &comment, &packet)) > 0)
    {
        check(ogg_stream_packetin(&stream, &packet) == 0, "header packet failed");
        pages(out, &stream, 1);
    }
    check(result == 0, "Theora headers failed");
    th_ycbcr_buffer planes = {{WIDTH, HEIGHT, WIDTH, yplane},
                              {WIDTH / 2, HEIGHT / 2, WIDTH / 2, uplane},
                              {WIDTH / 2, HEIGHT / 2, WIDTH / 2, vplane}};
    for (int frame = 0; frame < FRAMES; frame++)
    {
        memset(yplane, 28, sizeof(yplane));
        memset(uplane, 140, sizeof(uplane));
        memset(vplane, 120, sizeof(vplane));
        for (int x = 0; x < WIDTH; x += 40)
            rect(x, 104, 2, 188, 44, 140, 120);
        for (int y = 104; y < 292; y += 40)
            rect(0, y, WIDTH, 2, 44, 140, 120);
        int travel = (frame * 5) % 1088;
        int x = 48 + (travel > 544 ? 1088 - travel : travel);
        int y = 192 + (int)(62 * sin(frame * 0.105));
        rect(x - 28, y - 28, 56, 56, 160, 76, 70);
        rect(WIDTH - x - 20, 364 - y - 20, 40, 40, 120, 170, 220);
        rect(40, 316, 560, 12, 48, 128, 128);
        rect(40, 316, 560 * frame / (FRAMES - 1), 12, 200, 76, 90);
        digit(frame / 100, 256);
        digit((frame / 10) % 10, 304);
        digit(frame % 10, 352);
        check(th_encode_ycbcr_in(encoder, planes) == 0, "frame encode failed");
        while ((result = th_encode_packetout(encoder, frame == FRAMES - 1, &packet)) > 0)
        {
            check(ogg_stream_packetin(&stream, &packet) == 0, "video packet failed");
            pages(out, &stream, 0);
        }
        check(result == 0, "Theora packet output failed");
    }
    pages(out, &stream, 1);
    ogg_stream_clear(&stream);
    th_comment_clear(&comment);
    th_encode_free(encoder);
    check(fclose(out) == 0, "output close failed");
    return 0;
}
