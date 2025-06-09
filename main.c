#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/fifo.h>
#include <SDL.h>
#include <SDL_thread.h>
#include "libs/microlog/microlog.h"
#include "player/player.h"

int main(void) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        log_error("Could not initialize SDL: %s", SDL_GetError());
        return -1;
    }

    char *URL = "../data/videos/test.mp4";
    int response = 0;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;

    PlayerState *player = av_mallocz(sizeof(PlayerState));

    if (!player) {
        log_error("Could not allocate memory for player");
        response = -1;
        goto cleanup;
    }

    window = SDL_CreateWindow(URL,
                              SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED,
                              640,
                              480,
                              0
    );

    if (!window) {
        log_error("Could not create window: %s", SDL_GetError());
        response = -1;
        goto cleanup;
    }

    SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);

    // TODO: Try rendering with Vulkan
    renderer = SDL_CreateRenderer(window, -1,
                                  SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        log_error("Could not create renderer: %s", SDL_GetError());
        response = -1;
        goto cleanup;
    } else {
        SDL_RendererInfo renderer_info = {0};
        if (!SDL_GetRendererInfo(renderer, &renderer_info)) {
            log_info("Renderer name: %s", renderer_info.name);
        }
    }

    if (player_init(player, URL, renderer) < 0) {
        log_error("Could not initialize player");
        response = -1;
        goto cleanup;
    }
    log_info("Player initialized successfully");

    if (player_run(player)) {
        log_error("Could not run player");
        response = -1;
    }

    log_info("Player run completed");
cleanup:
    if (window) {
        SDL_DestroyWindow(window);
        log_info("Window destroyed");
    }
    if (player) {
        player_cleanup(player);
        av_free(player);
    }
    return response;
}
