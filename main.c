#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include "decoders/video_decoder.h"
#include "libs/microlog/microlog.h"
#include <SDL.h>
#include <SDL_thread.h>

void save_frames_as_ppm(AVFrame *frame, void *user_data) {
    static int frame_count = 0;
    char filename[32];


    sprintf(filename, "../out/ppm/frame%d.ppm", frame_count++);
    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        log_error("Could not open file %s", filename);
        return;
    }

    // Write header
    fprintf(file, "P6\n%d %d\n255\n", frame->width, frame->height);
    log_info("Wrote header for frame %d", frame_count);

    // Write pixel data
    for (int y = 0; y < frame->height; y++) {
        fwrite(frame->data[0] + y * frame->linesize[0], 1, frame->width * 3, file);
    }

    log_info("Saved frame %d to %s", frame_count, filename);
    fclose(file);
}

void display_image(ProcessingContext *ctx) {
    void *pixels;
    int pitch;

    //lock entire texture for writing
    if (SDL_LockTexture(ctx->texture, NULL, &pixels, &pitch) == 0) {
        // Prepare destination planes (YUV format)
        // Since YUV420P uses 3 channels , 3 planes hav to be set
        uint8_t *dst_planes[3];
        dst_planes[0] = pixels; // Y plane
        dst_planes[1] = pixels + ctx->decoder->codec_ctx->height * pitch; // U plane
        dst_planes[2] = dst_planes[1] + (ctx->decoder->codec_ctx->height * pitch / 4); // V plane

        int dst_linesize[3] = {pitch, pitch / 2, pitch / 2};

        // Convert the image into YUV format that SDL uses
        sws_scale(ctx->sws_ctx,
                  (uint8_t const * const *) ctx->frame_out->data,
                  ctx->frame_out->linesize,
                  0,
                  ctx->frame_out->height,
                  dst_planes,
                  dst_linesize);

        SDL_UnlockTexture(ctx->texture);
    }
    SDL_RenderClear(ctx->renderer);
    SDL_Rect rect = {
        0,
        0,
        ctx->decoder->codec_ctx->width,
        ctx->decoder->codec_ctx->height
    };
    SDL_RenderCopy(ctx->renderer, ctx->texture, NULL, &rect);
    SDL_RenderPresent(ctx->renderer);
}

int main(void) {
    const char *URL = "../data/videos/test.mp4";

    VideoDecoder *decoder = NULL;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    struct SwsContext *sws_ctx = NULL;
    ProcessingContext *ctx = malloc(sizeof(ProcessingContext));
    int response = 0;
    memset(ctx, 0, sizeof(ProcessingContext));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        log_error("Could not initialize SDL: %s", SDL_GetError());
        response = -1;
        goto cleanup;
    }

    decoder = video_decoder_init(URL);
    if (!decoder) {
        log_error("Could not initialize video decoder");
        response = -1;
        goto cleanup;
    }
    // Dump format information
    av_dump_format(decoder->fmt_ctx, 0, URL, 0);

    window = SDL_CreateWindow("Supposed to be name",
                              SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED,
                              decoder->codec_ctx->width,
                              decoder->codec_ctx->height,
                              0
    );
    if (!window) {
        log_error("Could not create window: %s", SDL_GetError());
        response = -1;
        goto cleanup;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        log_error("Could not create renderer: %s", SDL_GetError());
        response = -1;
        goto cleanup;
    }

    // Create YUV overlay on windows to input video
    texture = SDL_CreateTexture(renderer,
                                SDL_PIXELFORMAT_IYUV,
                                SDL_TEXTUREACCESS_STREAMING,
                                decoder->codec_ctx->width,
                                decoder->codec_ctx->height);
    if (!texture) {
        log_error("Could not create texture: %s", SDL_GetError());
        response = -1;
        goto cleanup;
    }
    sws_ctx = sws_getContext(decoder->codec_ctx->width,
                             decoder->codec_ctx->height,
                             decoder->codec_ctx->pix_fmt,
                             decoder->codec_ctx->width,
                             decoder->codec_ctx->height,
                             AV_PIX_FMT_YUV420P,
                             SWS_BILINEAR, NULL, NULL, NULL);

    if (!sws_ctx) {
        log_error("Could not create sws context");
        response = -1;
        goto cleanup;
    }

    ctx->texture = texture;
    ctx->renderer = renderer;
    ctx->sws_ctx = sws_ctx;


    SDL_Event event;
    int quit = 0;

    while (!quit) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                quit = 1;
            }
        }
        // TODO: When decoding is done, don't call decode again
        // Decode and display one frame
        if (decode(decoder, display_image, ctx)) {
            log_error("Decoding failed");
            response = -1;
            break;
        }
        break;
    }

cleanup:
    if (sws_ctx) {
        sws_freeContext(sws_ctx);
    }
    if (texture) {
        SDL_DestroyTexture(texture);
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
    if (decoder) {
        video_decoder_destroy(decoder);
    }
    free(ctx);
    SDL_Quit();


    return response;
}
