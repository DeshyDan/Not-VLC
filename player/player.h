//
// Created by Deshy on 2025/05/14.
//

#ifndef PLAYER_H
#define PLAYER_H

#include <SDL_render.h>
#include <SDL_thread.h>
#include <SDL_ttf.h>
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
    SDL_mutex *seek_mutex;
    SDL_mutex *pause_mutex;
    SDL_cond *pause_cond;
    int paused;
    int seek_complete;
    int seek_req;
    int seek_flags;
    int seek_rel;
    int64_t seek_pos;
    int *quit;

    SDL_Rect pause_button;
    SDL_Rect rewind_button;
    SDL_Rect forward_button;
    SDL_Texture *pause_texture;
    SDL_Texture *play_texture;
    SDL_Texture *rewind_texture;
    SDL_Texture *forward_texture;
    TTF_Font *font;
} PlayerState;


int player_init(PlayerState *player, const char *filename, SDL_Renderer *renderer);

void wait_if_paused();

void player_cleanup(PlayerState *player);

int player_run(PlayerState *player);
#endif //PLAYER_H
