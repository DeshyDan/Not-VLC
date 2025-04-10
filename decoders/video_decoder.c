#include "video_decoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include "../libs/microlog/microlog.h"

static VideoDecoder *video_decoder_alloc() {
    VideoDecoder *decoder = malloc(sizeof(VideoDecoder));
    if (!decoder) {
        return NULL;
    }
    decoder->fmt_ctx = avformat_alloc_context();
    decoder->codec_ctx = avcodec_alloc_context3(NULL);
    decoder->video_stream_index = -1;
    log_info("Successfully created decoder objects");
    return decoder;
}

static int find_stream_info(VideoDecoder *decoder) {
    if (avformat_find_stream_info(decoder->fmt_ctx, NULL) < 0) {
        log_error("Could not find stream information");
        return -1;
    }

    // Find video stream
    for (unsigned int i = 0; i < decoder->fmt_ctx->nb_streams; i++) {
        if (decoder->fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            decoder->video_stream_index = i;
            break;
        }
    }

    if (decoder->video_stream_index == -1) {
        log_error("Could not find video stream");
        return -1;
    }

    log_info("Found video stream: %d", decoder->video_stream_index);
    return 0;
}

static int setup_codec_context(VideoDecoder *decoder) {
    const AVStream *stream = decoder->fmt_ctx->streams[decoder->video_stream_index];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        log_error("Codec not found!");
        return -1;
    }

    decoder->codec_ctx = avcodec_alloc_context3(codec);
    if (!decoder->codec_ctx) {
        log_error("Failed to allocated codex context");
        return -1;
    }

    if (avcodec_parameters_to_context(decoder->codec_ctx, stream->codecpar) < 0) {
        log_error("Could not copy codec context");
        return -1;
    }
    log_info("Copied codex context");

    AVDictionary *opts = NULL;
    av_dict_set(&opts, "hwaccel", "videotoolbox", 0);
    if (avcodec_open2(decoder->codec_ctx, codec, &opts) < 0) {
        log_error("Failed to initialize codex context with HW acceleration");
        av_dict_free(&opts);
        return -1;
    }
    av_dict_free(&opts);
    log_info("Initialized codec context");

    return 0;
}

static int allocate_output_frame(VideoDecoder *decoder, AVFrame *frame, uint8_t **rgb_buffer) {
    int buffer_size = av_image_get_buffer_size(AV_PIX_FMT_RGB24,
                                               decoder->codec_ctx->width,
                                               decoder->codec_ctx->height, 1);
    *rgb_buffer = av_malloc(buffer_size * sizeof(uint8_t));
    if (!*rgb_buffer) {
        log_error("Failed to allocate RGB buffer");
        return -1;
    }

    if (av_image_fill_arrays(frame->data, frame->linesize, *rgb_buffer,
                             AV_PIX_FMT_RGB24, decoder->codec_ctx->width,
                             decoder->codec_ctx->height, 1) < 0) {
        log_error("Failed to setup output frame");
        av_free(*rgb_buffer);
        *rgb_buffer = NULL;
        return -1;
    }

    log_debug("Allocated output frame buffer (%d bytes)", buffer_size);
    return 0;
}

static void convert_frames_to_rgb(VideoDecoder *decoder, struct SwsContext *sws_ctx, AVFrame *frame,
                                  AVFrame *frame_rgb) {
    sws_scale(sws_ctx,
              (uint8_t const * const *) frame->data,
              frame->linesize,
              0,
              decoder->codec_ctx->height,
              frame_rgb->data,
              frame_rgb->linesize);
}

static void save_frames_as_ppm(AVFrame *frame, int width, int height, int iFrame) {
    char szFilename[32];

    sprintf(szFilename, "../out/ppm/frame%d.ppm", iFrame);
    FILE *file = fopen(szFilename, "wb");
    if (file == NULL) {
        log_error("Could not open file %s", szFilename);
        return;
    }

    // Write header
    fprintf(file, "P6\n%d %d\n255\n", width, height);
    log_info("Wrote header for frame %d", iFrame);

    // Write pixel data
    for (int y = 0; y < height; y++) {
        fwrite(frame->data[0] + y * frame->linesize[0], 1, width * 3, file);
    }

    log_info("Saved frame %d to %s", iFrame, szFilename);
    fclose(file);
}

static void cleanup_resources(AVFrame *frame, AVFrame *frame_rgb,
                              uint8_t *buffer, AVPacket *packet,
                              struct SwsContext *sws_ctx) {
    if (frame) av_frame_free(&frame);
    if (frame_rgb) av_frame_free(&frame_rgb);
    // if (buffer) av_free(&buffer);
    if (packet) av_packet_free(&packet);
    if (sws_ctx) sws_freeContext(sws_ctx);

    log_debug("Cleaned up decoder resources");
}

VideoDecoder *video_decoder_init(const char *url) {
    VideoDecoder *decoder = video_decoder_alloc();

    if (!decoder) {
        return NULL;
    }

    if (avformat_open_input(&decoder->fmt_ctx, url, 0, NULL) < 0) {
        log_error("Could not open source file %s", url);
        video_decoder_destroy(decoder);
        return NULL;
    }
    log_info("Opened input %s", url);


    if (find_stream_info(decoder) < 0) {
        log_error("Could not get stream information");
        video_decoder_destroy(decoder);

        return NULL;
    }

    if (setup_codec_context(decoder) < 0) {
        log_error("Could not setup codec context");
        video_decoder_destroy(decoder);
        return NULL;
    }

    return decoder;
}

int decode(VideoDecoder *decoder) {
    AVFrame *frame = av_frame_alloc();
    AVFrame *frame_rgb = av_frame_alloc();
    uint8_t *rgb_buffer = NULL;
    AVPacket *packet = av_packet_alloc();
    struct SwsContext *sws_ctx = sws_alloc_context();
    int response = 0;

    if (!frame || !frame_rgb || !packet) {
        log_error("Failed to allocate frames or packet");
        response = -1;
        goto cleanup;
    }

    // Allocate RGB buffer and frame
    if (allocate_output_frame(decoder, frame_rgb, &rgb_buffer) < 0) {
        log_error("Could not allocate RGB buffer");
        response = -1;
        goto cleanup;
    }

    sws_ctx = sws_getContext(decoder->codec_ctx->width,
                             decoder->codec_ctx->height,
                             decoder->codec_ctx->pix_fmt,
                             decoder->codec_ctx->width,
                             decoder->codec_ctx->height,
                             AV_PIX_FMT_RGB24,
                             SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws_ctx) {
        log_error("Failed to create sws context");
        response = -1;
        goto cleanup;
    }
    log_info("Created sws context");

    int i = 0;
    while (av_read_frame(decoder->fmt_ctx, packet) >= 0) {
        if (packet->stream_index == decoder->video_stream_index) {
            log_debug("Got video packet with size %d", packet->size);
            if (avcodec_send_packet(decoder->codec_ctx, packet) < 0) {
                log_error("Failed to send packet for decoding");
                av_packet_unref(packet);
                continue;
            }

            while (avcodec_receive_frame(decoder->codec_ctx, frame) >= 0) {
                convert_frames_to_rgb(decoder, sws_ctx, frame, frame_rgb);
                save_frames_as_ppm(frame_rgb, decoder->codec_ctx->width,
                                   decoder->codec_ctx->height, i++);
            }
        }
        av_packet_unref(packet);
    }

cleanup:
    cleanup_resources(frame, frame_rgb, rgb_buffer, packet, sws_ctx);
    return response;
}

void video_decoder_destroy(VideoDecoder *decoder) {
    if (!decoder) return;

    if (decoder->codec_ctx) {
        avcodec_free_context(&decoder->codec_ctx);
        decoder->codec_ctx = NULL;
        log_info("Freed codec context");
    }

    if (decoder->fmt_ctx) {
        avformat_close_input(&decoder->fmt_ctx);
        decoder->fmt_ctx = NULL;
        log_info("Closed input streams");
    }

    free(decoder);
    log_info("Destroyed video decoder");
}
