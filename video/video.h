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

typedef struct VideoPicture {
    int width;
    int height;
    int allocated;
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

    int *quit;
} VideoState;

int video_init(VideoState *video_state, AVFormatContext *fmt_ctx, SDL_Renderer *renderer);

void video_cleanup(VideoState *video);

int video_thread(void *userdata);

void video_display(VideoState *video);

void video_refresh_timer(void *userdata);

void schedule_refresh(VideoState *video, int delay);
#endif //VIDEO_H
