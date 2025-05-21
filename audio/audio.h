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

// Forward declarations
typedef struct PlayerState PlayerState;

typedef struct AudioState {
    AVFormatContext *format_context;
    AVStream *stream;
    int stream_index;
    AVCodecContext *codec_context;

    AVPacket packet;

    PacketQueue *audio_packet_queue;
    uint8_t audio_buffer[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
    unsigned int buffer_size;
    unsigned int buffer_index;
    struct SwrContext *swr_ctx;

    double audio_diff_cum;
    double audio_diff_avg_coef;
    double audio_diff_threshold;
    int audio_diff_avg_count;


    int *quit;
} AudioState;

int audio_init(AudioState *audio, PlayerState *player_state);

void audio_cleanup(AudioState *audio);

void sdl_audio_callback(void *userdata, Uint8 *stream, int len);
#endif //AUDIO_H
