#include <assert.h>
#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/fifo.h>
#include "decoders/decoder.h"
#include "libs/microlog/microlog.h"
#include <SDL.h>
#include <SDL_thread.h>
#include "display/player.h"
#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

int quit = 0;


int main(void) {
    char *URL = "../data/videos/test.mp4";
    SDL_Event event;

    int response = 0;


    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        log_error("Could not initialize SDL: %s", SDL_GetError());
        response = -1;
        goto cleanup;
    }

    VideoState *video_state = av_mallocz(sizeof(VideoState));
    video_state->picture_queue_mutex = SDL_CreateMutex();
    video_state->picture_queue_cond = SDL_CreateCond();
    video_state->URL = URL;

    schedule_refresh(video_state, 40); // pushes an FF_REFRESH_EVENT to event loop


    video_state->window = SDL_CreateWindow(URL,
                                           SDL_WINDOWPOS_CENTERED,
                                           SDL_WINDOWPOS_CENTERED,
                                           640,
                                           480,
                                           0
    );
    if (!video_state->window) {
        log_error("Could not create window: %s", SDL_GetError());
        response = -1;
        goto cleanup;
    }

    SDL_SetWindowFullscreen(video_state->window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    video_state->renderer = SDL_CreateRenderer(video_state->window, -1,
                                               SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!video_state->renderer) {
        log_error("Could not create renderer: %s", SDL_GetError());
        response = -1;
        goto cleanup;
    }
    SDL_Surface *screen = SDL_GetWindowSurface(video_state->window);
    video_state->screen = screen;

    video_state->parse_thread = SDL_CreateThread(decode, "decode thread", video_state);
    if (!video_state->parse_thread) {
        av_free(video_state);
        return -1;
    }
    while (true) {
        SDL_WaitEvent(&event);
        switch (event.type) {
            case FF_QUIT_EVENT:
            case SDL_QUIT:
                video_state->quit = 1;
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

cleanup:
    /*if (sws_ctx) {
        sws_freeContext(sws_ctx);
    }
    if (texture) {
        SDL_DestroyTexture(texture);
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
    }
    if (window) {
        SDL_DestroyWindow(window);
    }
    if (decoder) {
        decoder_destroy(decoder);
    }
    free(ctx);
    SDL_Quit();*/


    return response;
}
