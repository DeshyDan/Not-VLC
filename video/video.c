//
// Created by Deshy on 2025/05/17.
//
#include "video.h"
#include <SDL_events.h>
#include <SDL_timer.h>
#include "../libs/microlog/microlog.h"

static Uint32 sdl_refresh_timer_callback(Uint32 interval, void *user_data) {
    SDL_Event event;
    event.type = FF_REFRESH_EVENT;
    event.user.data1 = user_data;
    SDL_PushEvent(&event);
    return 0;
}

void schedule_refresh(VideoState *video_state, int delay) {
    log_info("Scheduling refresh with delay: %d", delay);
    SDL_AddTimer(delay, sdl_refresh_timer_callback, video_state);
}

void video_refresh_timer(void *userdata) {
    VideoState *video_state = (VideoState *) userdata;
    VideoPicture *video_picture;

    if (video_state->stream) {
        // Pull from the queue when we have something in the queue and then set timer so we display the next video frame
        //
        if (video_state->picture_queue_size == 0) {
            schedule_refresh(video_state, 1);
        } else {
            video_picture = &video_state->picture_queue[video_state->picture_queue_read_index];

            schedule_refresh(video_state, 80);
            video_display(video_state);

            // update queue for the next picture
            if (++video_state->picture_queue_read_index == VIDEO_PICTURE_QUEUE_SIZE) {
                video_state->picture_queue_read_index = 0;
            }

            SDL_LockMutex(video_state->picture_queue_mutex);
            video_state->picture_queue_size--;
            SDL_CondSignal(video_state->picture_queue_cond);
            SDL_UnlockMutex(video_state->picture_queue_mutex);
        }
    } else {
        schedule_refresh(video_state, 100);
    }
}

void video_display(VideoState *video_state) {
    if (!video_state || !video_state->video_texture || !video_state->stream) {
        log_error("Invalid video state or missing components");
        return;
    }

    VideoPicture *video_picture = &video_state->picture_queue[video_state->picture_queue_read_index];
    if (!video_picture->allocated) {
        log_warn("No frame available to display");
        return;
    }

    // Calculate display aspect ratio
    AVRational sar = video_state->stream->codecpar->sample_aspect_ratio;
    float aspect_ratio = (float) video_picture->width / (float) video_picture->height;
    if (sar.num != 0) {
        aspect_ratio = av_q2d(sar) * video_picture->width / video_picture->height;
    }

    int render_width;
    int render_height;
    SDL_GetRendererOutputSize(video_state->renderer, &render_width, &render_height);

    // Calculate target dimensions maintaining aspect ratio
    int width = render_height * aspect_ratio;
    int height = render_height;

    if (width > render_width) {
        width = render_width;
        height = width / aspect_ratio;
    }

    int x = (render_width - width) / 2;
    int y = (render_height - height) / 2;

    SDL_Rect rect = {x, y, width, height};

    SDL_LockMutex(video_state->screen_mutex);
    SDL_RenderClear(video_state->renderer);
    SDL_RenderCopy(video_state->renderer, video_state->video_texture, NULL, &rect);
    SDL_RenderPresent(video_state->renderer);
    SDL_UnlockMutex(video_state->screen_mutex);
}
