//
// Created by Deshy on 2025/05/17.
//

#ifndef VIDEO_H
#define VIDEO_H
#include <SDL_mutex.h>
#include <SDL_render.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include "../utils/packet_queue.h"

#define VIDEO_PICTURE_QUEUE_SIZE 1
#define FF_REFRESH_EVENT (SDL_USEREVENT)
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)

// Forward declarations
typedef struct PlayerState PlayerState;

typedef double (*GetAudioClockFn)(void *userdata);

typedef struct VideoPicture {
    int width;
    int height;
    int allocated;
    double presentation_time_stamp;
} VideoPicture;

typedef struct VideoState {
    int stream_index;
    AVStream *stream;

    AVCodecContext *codec_context;
    struct SwsContext *sws_ctx;

    PacketQueue *packet_queue;
    VideoPicture picture_queue[VIDEO_PICTURE_QUEUE_SIZE];
    int picture_queue_size;
    int picture_queue_read_index;
    int picture_queue_write_index;
    SDL_mutex *picture_queue_mutex;
    SDL_cond *picture_queue_cond;

    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_mutex *screen_mutex;

    double frame_last_presentation_time_stamp;
    double frame_last_delay;
    double frame_timer;
    double video_current_pts;
    int64_t video_current_pts_time;

    GetAudioClockFn get_audio_clock;
    void *audio_clock_userdata;
    int *quit;
} VideoState;

void set_get_audio_clock_fn(GetAudioClockFn fn, VideoState *video_state, void *userdata);

int video_init(VideoState *video_state, PlayerState *player_state, SDL_Renderer *renderer);

int video_state_reset(VideoState *video_state);

void video_cleanup(VideoState *video);

int video_thread(void *userdata);

void video_display(VideoState *video);

void video_refresh_timer(void *userdata);

void schedule_refresh(VideoState *video, int delay);
#endif //VIDEO_H
