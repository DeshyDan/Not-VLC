//
// Created by Deshy on 2025/04/09.
//

#ifndef VIDEO_DECODER_H
#define VIDEO_DECODER_H

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

typedef struct VideoDecoder {
    /**
     * Exports all information about the file being read or written
     */
    AVFormatContext *fmt_ctx;
    /**
     * Stores everything related to the codex
     */
    AVCodecContext *codec_ctx;
    /**
     * The index of the video stream in the format context
     */
    int video_stream_index;
} VideoDecoder;

/**
 * Initializes a video decoder with the given URL. Objects returned contains all context
 * about the video stream.
 * @param url URL of stream to open
 * @return VideoDecoder objects
 * If error occurs, null is returned
 */
VideoDecoder *video_decoder_init(const char *url);

/**
 * Closes the video decoder and frees all resources.
 * @param decoder Object to destroy
 */
void video_decoder_destroy(VideoDecoder *decoder);

#endif //VIDEO_DECODER_H
