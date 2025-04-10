//
// Created by Deshy on 2025/04/09.
//

#ifndef VIDEO_DECODER_H
#define VIDEO_DECODER_H

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

typedef void (*FrameProcessor)(AVFrame *frame, void *user_data);

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
* 1. Reads chunkds of packets from the video stream
* 2. Turns comprersed packets into raw frames
* 3. converts raw frames into RGB frames
* 4. Saves the RGB frame to a file
*
* Function also takes in a callback to perform operations on decoded frames
*/
int decode(VideoDecoder *decoder, FrameProcessor processor, void *user_data);

/**
 * Closes the video decoder and frees all resources.
 * @param decoder Object to destroy
 */
void video_decoder_destroy(VideoDecoder *decoder);

#endif //VIDEO_DECODER_H
