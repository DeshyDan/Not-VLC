//
// Created by Deshy on 2025/05/14.
//

#include "player.h"

#include <SDL.h>
#include <SDL_events.h>
#include <libavutil/time.h>

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

    sync_init(DEFAULT_AV_SYNC_TYPE, player_state);

    return 0;
}

static void stream_seek(PlayerState *player_state, int64_t pos, int64_t rel, int flags) {
    SDL_LockMutex(player_state->seek_mutex);

    if (!player_state->seek_req && player_state->seek_complete) {
        player_state->seek_pos = pos;
        player_state->seek_flags = flags;
        player_state->seek_rel = rel;
        player_state->seek_req = 1;
        player_state->seek_complete = 0;

        sync_reset_clock(pos / (double) AV_TIME_BASE);
    }

    SDL_UnlockMutex(player_state->seek_mutex);
}

static void handle_seek(PlayerState *player_state, double incr) {
    if (!player_state) return;

    double pos;
    SDL_LockMutex(player_state->seek_mutex);
    if (sync_state->av_sync_type == AV_SYNC_AUDIO_MASTER ||
        sync_state->av_sync_type == AV_SYNC_VIDEO_MASTER) {
        pos = get_master_clock();
    } else {
        pos = get_external_clock();
    }
    SDL_UnlockMutex(player_state->seek_mutex);

    pos += incr;
    if (pos < 0) pos = 0;

    double duration = player_state->format_context->duration / (double) AV_TIME_BASE;
    if (pos > duration) pos = duration - 1.0;

    int64_t seek_target = (int64_t) (pos * AV_TIME_BASE);

    int seek_flags = (incr < 0) ? AVSEEK_FLAG_BACKWARD : 0;
    seek_flags |= AVSEEK_FLAG_ANY;
    stream_seek(player_state, seek_target, incr, seek_flags);
}

static void flush_codec_buffers(PlayerState *player_state) {
    if (player_state->audio_state->codec_context) {
        avcodec_flush_buffers(player_state->audio_state->codec_context);
    }

    if (player_state->video_state->codec_context) {
        avcodec_flush_buffers(player_state->video_state->codec_context);
    }
}

static void flush_queues(PlayerState *player_state, int64_t seek_target) {
    if (player_state->audio_state->stream_index >= 0) {
        packet_queue_flush(player_state->audio_packet_queue);
        sync_state->audio_clock = seek_target / (double) AV_TIME_BASE;

        AVPacket *flush_pkt = av_packet_alloc();
        flush_pkt->data = NULL;
        packet_queue_put(player_state->audio_packet_queue, flush_pkt);
        av_packet_free(&flush_pkt);
        log_info("Flushed audio queue");
    }

    if (player_state->video_state->stream_index >= 0) {
        packet_queue_flush(player_state->video_packet_queue);

        AVPacket *flush_pkt = av_packet_alloc();
        flush_pkt->data = NULL;
        packet_queue_put(player_state->video_packet_queue, flush_pkt);
        av_packet_free(&flush_pkt);

        video_state_reset(player_state->video_state);
        log_info("Flushed video queue");
    }
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
            int seek_flags = player_state->seek_flags;

            if (player_state->audio_state->stream_index >= 0) {
                stream_index = player_state->audio_state->stream_index;
            } else if (player_state->video_state->stream_index >= 0) {
                stream_index = player_state->video_state->stream_index;
            }

            if (stream_index >= 0) {
                seek_target = av_rescale_q(seek_target,
                                           AV_TIME_BASE_Q,
                                           player_state->format_context->streams[stream_index]->time_base);

                int64_t seek_min = player_state->seek_rel > 0
                                       ? seek_target - player_state->seek_rel + 2
                                       : INT64_MIN;
                int64_t seek_max = player_state->seek_rel > 0 ? seek_target + player_state->seek_rel - 2 : INT64_MAX;
                if (avformat_seek_file(player_state->format_context, stream_index, seek_min, seek_target, seek_max,
                                       seek_flags) < 0) {
                    log_error("Error while seeking");
                } else {
                    flush_codec_buffers(player_state);

                    flush_queues(player_state, seek_target);
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
