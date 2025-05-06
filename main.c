#include <assert.h>
#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/fifo.h>
#include "decoders/decoder.h"
#include "libs/microlog/microlog.h"
#include <SDL.h>
#include <SDL_thread.h>

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000


void display_image(ProcessingContext *ctx) {
    void *pixels;
    int pitch;

    //lock entire texture for writing
    if (SDL_LockTexture(ctx->texture, NULL, &pixels, &pitch) == 0) {
        // Prepare destination planes (YUV format)
        // Since YUV420P uses 3 channels , 3 planes hav to be set
        uint8_t *dst_planes[3];
        dst_planes[0] = pixels; // Y plane
        dst_planes[1] = pixels + ctx->decoder->video_codec_ctx->height * pitch; // U plane
        dst_planes[2] = dst_planes[1] + (ctx->decoder->video_codec_ctx->height * pitch / 4); // V plane

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
        ctx->decoder->video_codec_ctx->width,
        ctx->decoder->video_codec_ctx->height
    };
    SDL_RenderCopy(ctx->renderer, ctx->texture, NULL, &rect);
    SDL_RenderPresent(ctx->renderer);
    SDL_Delay(10);
}

void packet_queue_init(PacketQueue *queue) {
    memset(queue, 0, sizeof(PacketQueue));
    queue->packet_fifo = av_fifo_alloc2(32, sizeof(AVPacket *), AV_FIFO_FLAG_AUTO_GROW);
    queue->mutex = SDL_CreateMutex();
    queue->cond = SDL_CreateCond();
}


int quit = 0;

int packet_queue_get(PacketQueue *queue, AVPacket *packet, int block) {
    AVPacket *pkt = av_packet_alloc();
    int ret = 0;

    SDL_LockMutex(queue->mutex);
    while (true) {
        if (av_fifo_read(queue->packet_fifo, &pkt, 1) >= 0) {
            queue->nb_packets--;
            queue->size -= pkt->size;
            av_packet_move_ref(packet, pkt);
            av_packet_free(&pkt);
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            SDL_CondWait(queue->cond, queue->mutex);
        }
    }
    SDL_UnlockMutex(queue->mutex);
    return ret;
}

int audio_decode_frame(AVCodecContext *audio_codec_ctx, PacketQueue *packet_queue, uint8_t *audio_buf, int buf_size) {
    AVPacket packet;
    AVFrame *frame = av_frame_alloc();
    int data_size = 0;

    while (1) {
        if (packet_queue_get(packet_queue, &packet, 1) <= 0) {
            av_frame_free(&frame);
            return -1;
        }

        if (avcodec_send_packet(audio_codec_ctx, &packet) < 0) {
            av_packet_unref(&packet);
            continue;
        }

        while (avcodec_receive_frame(audio_codec_ctx, frame) >= 0) {
            data_size = av_samples_get_buffer_size(
                NULL,
                audio_codec_ctx->ch_layout.nb_channels,
                frame->nb_samples,
                audio_codec_ctx->sample_fmt,
                1);

            if (data_size > buf_size) {
                log_error("Audio buffer too small");
                av_frame_free(&frame);
                av_packet_unref(&packet);
                return -1;
            }

            memcpy(audio_buf, frame->data[0], data_size);
            av_frame_free(&frame);
            av_packet_unref(&packet);
            return data_size;
        }

        av_packet_unref(&packet);
    }
}

/**
 * SDL calls this function whenever it needs more audio data to play
 *
 * @param userdata pointer to give to SDL
 * @param stream the buffer we are writing audio data to
 * @param len size of the buffer we are handling
 */
void audio_callback(void *userdata, Uint8 *stream, int len) {
    ProcessingContext *ctx = (ProcessingContext *) userdata;
    AVCodecContext *audio_codec_ctx = ctx->decoder->audio_codec_context;

    uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
    unsigned int audio_buf_size = 0;
    unsigned int audio_buf_index = 0; // Represents the current position in the buffer


    while (len > 0) {
        // fill buffer until we have enough data to decode.
        if (audio_buf_index >= audio_buf_size) {
            int audio_size = audio_decode_frame(audio_codec_ctx, &ctx->audio_queue, audio_buf, sizeof(audio_buf));

            if (audio_size < 0) {
                audio_buf_size = 1024;
                memset(audio_buf, 0, audio_buf_size);
            } else {
                audio_buf_size = audio_size;
            }
            audio_buf_index = 0;
        }

        int length = audio_buf_size - audio_buf_index;

        if (length > len) {
            length = len;
        }

        memcpy(stream, (uint8_t *) audio_buf + audio_buf_index, length);
        len -= length;
        stream += length;
        audio_buf_index += length;
    }
}


int main(void) {
    const char *URL = "../data/videos/test.mp4";

    VideoDecoder *decoder = NULL;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    SDL_AudioSpec wanted_spec;
    SDL_AudioSpec spec;
    struct SwsContext *sws_ctx = NULL;
    ProcessingContext *ctx = malloc(sizeof(ProcessingContext));
    int response = 0;
    memset(ctx, 0, sizeof(ProcessingContext));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        log_error("Could not initialize SDL: %s", SDL_GetError());
        response = -1;
        goto cleanup;
    }

    decoder = decoder_init(URL);
    ctx->decoder = decoder;
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
                              decoder->video_codec_ctx->width,
                              decoder->video_codec_ctx->height,
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
                                decoder->video_codec_ctx->width,
                                decoder->video_codec_ctx->height);
    if (!texture) {
        log_error("Could not create texture: %s", SDL_GetError());
        response = -1;
        goto cleanup;
    }

    // Note: SWS context is for image scaling and format conversions.
    // Converting from source format to YUV420P to display on the SDL windows
    sws_ctx = sws_getContext(decoder->video_codec_ctx->width,
                             decoder->video_codec_ctx->height,
                             decoder->video_codec_ctx->pix_fmt,
                             decoder->video_codec_ctx->width,
                             decoder->video_codec_ctx->height,
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

    wanted_spec.freq = decoder->audio_codec_context->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = decoder->audio_codec_context->ch_layout.nb_channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
    wanted_spec.callback = audio_callback;
    wanted_spec.userdata = ctx;

    // TODO: The new, more powerful, and preferred way to do this is SDL_OpenAudioDevice()
    if (SDL_OpenAudio(&wanted_spec, &spec) < 0) {
        log_error("Failed to open audio: %s", SDL_GetError());
        return 0;
    }

    packet_queue_init(&ctx->audio_queue);
    SDL_PauseAudio(0);
    SDL_Event event;
    while (!quit) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                quit = 1;
            }
        }
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
        decoder_destroy(decoder);
    }
    free(ctx);
    SDL_Quit();


    return response;
}
