//
// Created by Deshy on 2025/05/14.
//

#include "player.h"

#include <SDL.h>
#include <SDL_events.h>

#include "../libs/microlog/microlog.h"
#include "../audio/audio.h"
#include "../utils/sync.h"
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
    player_state->seek_mutex = SDL_CreateMutex();
    if (!player_state->seek_mutex) {
        log_error("Could not create seek mutex");
        return -1;
    }
    player_state->seek_complete = 1;
    player_state->quit = malloc(sizeof(int));
    *player_state->quit = 0;
    video_state->quit = player_state->quit;
    audio_state->quit = player_state->quit;

    sync_init(DEFAULT_AV_SYNC_TYPE);

    return 0;
}

static void stream_seek(PlayerState *player_state, int64_t pos, double incr) {
    SDL_LockMutex(player_state->seek_mutex);

    if (!player_state->seek_req && player_state->seek_complete) {
        player_state->seek_pos = pos;
        player_state->seek_flags = (incr < 0) ? AVSEEK_FLAG_BACKWARD : 0;
        player_state->seek_req = 1;
        player_state->seek_complete = 0;
    }

    SDL_UnlockMutex(player_state->seek_mutex);
}

static void handle_seek(PlayerState *player_state, double incr) {
    if (!player_state) return;

    double pos;
    SDL_LockMutex(player_state->seek_mutex);
    pos = get_master_clock(player_state);
    SDL_UnlockMutex(player_state->seek_mutex);

    pos += incr;
    if (pos < 0) pos = 0;

    double duration = player_state->format_context->duration / (double) AV_TIME_BASE;
    if (pos > duration) pos = duration - 1.0;

    stream_seek(player_state, (int64_t) (pos * AV_TIME_BASE), incr);
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
        if (*player_state->quit) {
            log_info("Quiting player");
            break;
        }
        if (player_state->seek_req) {
            SDL_LockMutex(player_state->seek_mutex);
            int stream_index = -1;
            int64_t seek_target = player_state->seek_pos;

            if (player_state->audio_state->stream_index >= 0) {
                stream_index = player_state->audio_state->stream_index;
            } else if (player_state->video_state->stream_index >= 0) {
                stream_index = player_state->video_state->stream_index;
            }

            if (stream_index >= 0) {
                seek_target = av_rescale_q(seek_target,
                                           AV_TIME_BASE_Q,
                                           player_state->format_context->streams[stream_index]->time_base);
            }
            if (av_seek_frame(player_state->format_context, stream_index, seek_target, player_state->seek_flags) < 0) {
                log_error("Error while seeking");
            } else {
                if (player_state->audio_state->stream_index >= 0) {
                    sync_state->audio_clock = seek_target / (double) AV_TIME_BASE;
                    packet_queue_flush(player_state->audio_packet_queue);
                    AVPacket *audio_flush_packet = av_packet_alloc();
                    if (!audio_flush_packet) {
                        log_error("failed to allocate flush packet");
                        return -1;
                    }

                    audio_flush_packet->data = NULL;
                    packet_queue_put(player_state->audio_packet_queue, audio_flush_packet);
                    av_packet_free(&audio_flush_packet);
                    log_info("Flushed audio queue");
                }

                if (player_state->video_state->stream_index >= 0) {
                    packet_queue_flush(player_state->video_packet_queue);

                    AVPacket *video_flush_packet = av_packet_alloc();
                    if (!video_flush_packet) {
                        log_error("failed to allocate flush packet");
                        return -1;
                    }

                    video_flush_packet->data = NULL;
                    packet_queue_put(player_state->video_packet_queue, video_flush_packet);
                    av_packet_free(&video_flush_packet);
                    log_info("Flushed video queue");
                    SDL_Delay(10);
                }
            }
            player_state->seek_req = 0;
            player_state->seek_complete = 1;
            SDL_UnlockMutex(player_state->seek_mutex);
        }
        SDL_WaitEvent(&event);
        switch (event.type) {
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                    case SDLK_LEFT:
                        handle_seek(player_state, -10.0);
                        break;
                    case SDLK_RIGHT:
                        handle_seek(player_state, 10.0);
                        break;
                    case SDLK_UP:
                        handle_seek(player_state, 60.0);
                        break;
                    case SDLK_DOWN:
                        handle_seek(player_state, -60.0);
                        break;
                    default:
                        break;
                }
                break;

            case FF_QUIT_EVENT:
            case SDL_QUIT:
                if (player_state && player_state->quit) {
                    *player_state->quit = 1;
                }
                SDL_Quit();
                return 0;
            case FF_REFRESH_EVENT:
                if (event.user.data1) {
                    video_refresh_timer(event.user.data1);
                }
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
    if (player_state->seek_mutex) {
        SDL_DestroyMutex(player_state->seek_mutex);
    }

    free(player_state->quit);
}
