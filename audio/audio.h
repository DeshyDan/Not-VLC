//
// Created by Deshy on 2025/05/14.
//

#ifndef AUDIO_H
#define AUDIO_H

#include <SDL_audio.h>
#include "../utils/packet_queue.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

typedef struct AudioState {
    AVStream *stream;
    int stream_index;
    AVCodecContext *codec_ctx;

    PacketQueue audio_packet_queue;
    uint8_t audio_buffer[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
    unsigned int buffer_size;
    unsigned int buffer_index;
    struct SwsContext *sws_context;
} AudioState;

#endif //AUDIO_H
