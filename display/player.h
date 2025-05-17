//
// Created by Deshy on 2025/05/14.
//

#ifndef PLAYER_H
#define PLAYER_H

#include "../audio/audio.h"
#include "../video/video.h"
#include "../utils/packet_queue.h"

typedef struct PlayerState {
    AVFormatContext *format_context;

    AudioState audio;
    VideoState video;

    PacketQueue audio_queue;
    PacketQueue video_queue;

    SDL_Thread *decode_thread;
} PlayerState;


int player_init(PlayerState *player, const char *filename, SDL_Renderer *renderer);

void player_cleanup(PlayerState *player);

int player_run(PlayerState *player, SDL_Window *window, SDL_Renderer *renderer);
#endif //PLAYER_H
