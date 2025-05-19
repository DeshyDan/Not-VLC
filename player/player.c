//
// Created by Deshy on 2025/05/14.
//

#include "player.h"

#include <SDL.h>
#include <SDL_events.h>

#include "../libs/microlog/microlog.h"
#include "../audio/audio.h"
#include "../video/video.h"

int player_init(PlayerState *player_state, const char *filename, SDL_Renderer *renderer) {
    player_state->format_context = avformat_alloc_context();

    if (!player_state->format_context) {
        log_error("Could not allocate player format context");
        return -1;
    }

    // Opening the video and reading the video file. Info is stored in the AVFormatContext
    if (avformat_open_input(&player_state->format_context, filename, 0, NULL) < 0) {
        log_error("Could not open source file %s", filename);
        return -1;
    }
    log_info("Opened source file %s", filename);

    if (avformat_find_stream_info(player_state->format_context, NULL) < 0) {
        log_error("Could not find stream information");
        return -1;
    }

    VideoState *video_state = malloc(sizeof(VideoState));
    AudioState *audio_state = malloc(sizeof(AudioState));

    player_state->video_state = video_state;
    player_state->audio_state = audio_state;

    if (!video_state || !audio_state) {
        log_error("Could not allocate video/audio state");
        return -1;
    }

    if (audio_init(audio_state, player_state) < 0) {
        log_error("Could not initialize audio");
        return -1;
    }

    if (video_init(video_state, player_state, renderer) < 0) {
        log_error("Could not initialize video");
        return -1;
    }

    player_state->audio_packet_queue = audio_state->audio_packet_queue;
    player_state->video_packet_queue = video_state->packet_queue;

    player_state->quit = 0;
    video_state->quit = player_state->quit;
    audio_state->quit = player_state->quit;


    return 0;
}


int player_run(PlayerState *player_state) {
    SDL_Event event;
    player_state->packet_queueing_thread = SDL_CreateThread(packet_queueing_thread, "packet queuing thread",
                                                            player_state);
    player_state->video_decode_thread = SDL_CreateThread(video_thread, "video thread", player_state);
    if (!player_state->video_decode_thread) {
        log_error("Could not create video decode thread");
        return -1;
    }

    schedule_refresh(player_state->video_state, 40); // pushes an FF_REFRESH_EVENT to event loop

    while (true) {
        SDL_WaitEvent(&event);
        switch (event.type) {
            case FF_QUIT_EVENT:
            case SDL_QUIT:
                *player_state->quit = 1;
                SDL_Quit();
                return 0;
                break;
            case FF_REFRESH_EVENT:
                video_refresh_timer(event.user.data1);
                break;
            default:
                break;
        }
    }
}

void player_cleanup(PlayerState *player_state) {
    if (player_state->format_context) {
        avformat_close_input(&player_state->format_context);
        log_info("Closed player format context");
    }
    if (player_state->video_state) {
        video_cleanup(player_state->video_state);
        free(player_state->video_state);
        log_info("Freed player video state");
    }
    if (player_state->audio_state) {
        audio_cleanup(player_state->audio_state);
        free(player_state->audio_state);
        log_info("Freed player audio state");
    }
}
