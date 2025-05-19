//
// Created by Deshy on 2025/05/14.
//

#ifndef PLAYER_H
#define PLAYER_H

#include <SDL_render.h>
#include <SDL_thread.h>
#include <libavformat/avformat.h>
#include "../utils/packet_queue.h"

// Forward declarations
typedef struct AudioState AudioState;
typedef struct VideoState VideoState;

typedef struct PlayerState {
    AVFormatContext *format_context;

    AudioState *audio_state;
    VideoState *video_state;

    PacketQueue *audio_packet_queue;
    PacketQueue *video_packet_queue;

    SDL_Thread *video_decode_thread;
    SDL_Thread *packet_queueing_thread;
    int *quit;
} PlayerState;


int player_init(PlayerState *player, const char *filename, SDL_Renderer *renderer);

void player_cleanup(PlayerState *player);

int player_run(PlayerState *player);
#endif //PLAYER_H
