#include "video_decoder.h"
#include <stdio.h>
#include <stdlib.h>
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
        log_error("Unsupported codec!");
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

    if (avcodec_open2(decoder->codec_ctx, codec, NULL) < 0) {
        log_error("Failed to initialize codex context");
        return -1;
    }
    log_info("Initialized codec context");

    return 0;
}

VideoDecoder *video_decoder_init(const char *url) {
    VideoDecoder *decoder = video_decoder_alloc();

    if (!decoder) {
        return NULL;
    }

    if (avformat_open_input(&decoder->fmt_ctx, url, 0, NULL) < 0) {
        log_error("Could not open source file %s", url);
        return NULL;
    }
    log_info("Opened input %s", url);


    if (find_stream_info(decoder) < 0) {
        log_error("Could not get stream information");
        return NULL;
    }

    if (setup_codec_context(decoder) < 0) {
        log_error("Could not setup codec context");
        return NULL;
    }

    return decoder;
}

void video_decoder_destroy(VideoDecoder *decoder) {
    if (!decoder) return;

    if (decoder->codec_ctx) {
        avcodec_free_context(&decoder->codec_ctx);
        log_info("Freed codex context");
    }

    if (decoder->fmt_ctx) {
        avformat_close_input(&decoder->fmt_ctx);
        log_info("Closed input streams");
    }

    free(decoder);
    log_info("Destroyed video decoder");
}
