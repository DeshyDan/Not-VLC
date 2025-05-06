//
// Created by Deshy on 2025/04/09.
//

#ifndef VIDEO_DECODER_H
#define VIDEO_DECODER_H

#include <SDL_mutex.h>
#include <SDL_render.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/fifo.h>


typedef struct VideoDecoder {
    /**
     * Exports all information about the file being read or written
     */
    AVFormatContext *fmt_ctx;
    /**
     * Stores everything related to the codex
     */
    AVCodecContext *video_codec_ctx;

    AVCodecContext *audio_codec_ctx_original;
    AVCodecContext *audio_codec_context;
    /**
     * The index of the video stream in the format context
     */
    int video_stream_index;
    int audio_stream_index;
} VideoDecoder;

typedef struct PacketQueue {
    AVFifo *packet_fifo;
    int nb_packets;
    int size; // total size of packets in bytes
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

typedef struct ProcessingContext {
    VideoDecoder *decoder;
    AVFrame *frame_out;
    SDL_Texture *texture;
    SDL_Renderer *renderer;
    struct SwsContext *sws_ctx;
    int frame_count;
    PacketQueue audio_queue;
} ProcessingContext;

typedef void (*FrameProcessor)(ProcessingContext *ctx);

/**
 * Initializes a decoder with the given URL. Objects returned contains all context
 * about the video and audio stream.
 * @param url URL of stream to open
 * @return VideoDecoder objects
 * If error occurs, null is returned
 */
VideoDecoder *decoder_init(const char *url);


/**
*
* Function also takes in a callback to perform operations on decoded frames
*/
int decode(VideoDecoder *decoder, FrameProcessor processor, ProcessingContext *ctx);

/**
 * Closes the decoder and frees all resources.
 * @param decoder Object to destroy
 */
void decoder_destroy(VideoDecoder *decoder);

#endif //VIDEO_DECODER_H
