//
// Created by Deshy on 2025/04/09.
//

#ifndef VIDEO_DECODER_H
#define VIDEO_DECODER_H

#include <SDL_mutex.h>
#include <SDL_render.h>
#include <SDL_thread.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/fifo.h>
#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000
#define VIDEO_PICTURE_QUEUE_SIZE 1
#define FF_REFRESH_EVENT (SDL_USEREVENT)
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)


typedef struct PacketQueue {
    AVFifo *packet_fifo;
    int nb_packets;
    int size; // total size of packets in bytes
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

typedef struct VideoPicture {
    int width;
    int height;
    int allocated;
} VideoPicture;

typedef struct VideoState {
    AVFormatContext *format_context;

    char *URL;

    int video_stream_index;
    int audio_stream_index;

    AVStream *audio_stream;
    AVCodecContext *audio_codec_context;
    struct SwrContext *swr_ctx;
    PacketQueue audio_packet_queue;
    uint8_t audio_buffer[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
    unsigned int audio_buffer_size;
    unsigned int audio_buffer_index;
    AVPacket audio_packet;
    uint8_t audio_packet_data;
    uint8_t audio_packet_size;
    AVStream *video_stream;
    struct SwsContext *sws_ctx;

    VideoPicture picture_queue[VIDEO_PICTURE_QUEUE_SIZE];
    AVCodecContext *video_codec_ctx;
    PacketQueue video_packet_queue;

    int picture_queue_size;
    int picture_queue_read_index;
    int picture_queue_write_index;
    SDL_mutex *picture_queue_mutex;
    SDL_cond *picture_queue_cond;

    SDL_Thread *parse_thread;
    SDL_Thread *video_thread;
    SDL_Thread *audio_thread;

    SDL_Texture *video_texture;
    SDL_Window *window;
    SDL_Renderer *renderer;
    int quit;

    SDL_Surface *screen;
    SDL_mutex *screen_mutex;
} VideoState;


int decode(void *userdata);


void video_display(VideoState *video_state);

void video_refresh_timer(void *userdata);

void schedule_refresh(VideoState *video_state, int delay);
#endif //VIDEO_DECODER_H
