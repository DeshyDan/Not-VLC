//
// Created by Deshy on 2025/05/17.
//
#include "video.h"
#include <SDL_events.h>
#include <SDL_timer.h>
#include <libswscale/swscale.h>
#include <libavutil/time.h>
#include "../libs/microlog/microlog.h"
#include "../player/player.h"
#include  "../audio/audio.h"
#include "../utils/sync.h"

#define AV_SYNC_THRESHOLD 0.01
#define AV_NOSYNC_THRESHOLD 10.0

void set_get_audio_clock_fn(GetAudioClockFn fn, VideoState *video_state, void *userdata) {
    AudioState *audio_state = (AudioState *) userdata;
    video_state->get_audio_clock = fn;
    video_state->audio_clock_userdata = audio_state;
    log_info("Set get audio clock function and userdata");
}

static Uint32 sdl_refresh_timer_callback(Uint32 interval, void *user_data) {
    SDL_Event event;
    event.type = FF_REFRESH_EVENT;
    event.user.data1 = user_data;
    SDL_PushEvent(&event);
    log_info("Pushed refresh event");
    return 0;
}

void schedule_refresh(VideoState *video_state, int delay) {
    log_info("Scheduling refresh with delay: %d", delay);
    SDL_AddTimer(delay, sdl_refresh_timer_callback, video_state);
}

void video_refresh_timer(void *userdata) {
    VideoState *video_state = (VideoState *) userdata;
    VideoPicture *video_picture;

    double actual_delay;
    double delay;
    double sync_threshold;
    double ref_clock;
    double diff;

    if (video_state->stream) {
        // Pull from the queue when we have something in the queue and then set timer so we display the next video frame
        if (video_state->picture_queue_size == 0) {
            log_warn("No picture in queue");
            schedule_refresh(video_state, 1);
        } else {
            video_picture = &video_state->picture_queue[video_state->picture_queue_read_index];

            video_state->video_current_pts = video_picture->presentation_time_stamp;
            video_state->video_current_pts_time = av_gettime();

            delay = video_picture->presentation_time_stamp - video_state->frame_last_presentation_time_stamp;
            if (delay < 0 || delay >= 1.0) {
                log_warn("Incorrect delay, using previous one");
                delay = video_state->frame_last_delay;
            }

            video_state->frame_last_delay = delay;
            video_state->frame_last_presentation_time_stamp = video_picture->presentation_time_stamp;

            if (sync_state->av_sync_type == AV_SYNC_AUDIO_MASTER) {
                ref_clock = video_state->get_audio_clock(video_state->audio_clock_userdata);
            } else if (sync_state->av_sync_type == AV_SYNC_EXTERNAL_MASTER) {
                ref_clock = get_external_clock();
            } else {
                ref_clock = video_picture->presentation_time_stamp;
            }

            diff = video_picture->presentation_time_stamp - ref_clock;
            sync_threshold = (delay > AV_SYNC_THRESHOLD) ? delay : AV_SYNC_THRESHOLD;

            if (fabs(diff) < AV_NOSYNC_THRESHOLD) {
                if (diff <= -sync_threshold) {
                    delay = 0;
                } else if (diff >= sync_threshold) {
                    delay = 2 * delay;
                }
            }

            video_state->frame_timer += delay;
            actual_delay = video_state->frame_timer - (av_gettime() / 1000000.0);
            // note :av_gettime returns the time in microseconds

            if (actual_delay < 0.010) {
                log_warn("Actual delay too small, setting to 0.010");
                actual_delay = 0.010;
            }

            schedule_refresh(video_state, (int) (actual_delay * 1000 + 0.5));
            video_display(video_state);

            // update queue for the next picture
            if (++video_state->picture_queue_read_index == VIDEO_PICTURE_QUEUE_SIZE) {
                log_info("Resetting picture queue read index");
                video_state->picture_queue_read_index = 0;
            }

            SDL_LockMutex(video_state->picture_queue_mutex);
            video_state->picture_queue_size--;
            log_info("Decremented picture queue size: %d", video_state->picture_queue_size);
            SDL_CondSignal(video_state->picture_queue_cond);
            SDL_UnlockMutex(video_state->picture_queue_mutex);
        }
    } else {
        schedule_refresh(video_state, 100);
    }
}

void alloc_picture(void *userdata) {
    VideoState *video_state = (VideoState *) userdata;
    VideoPicture *video_picture;

    video_picture = &video_state->picture_queue[video_state->picture_queue_write_index];

    if (video_state->texture) {
        log_warn("Releasing old texture");
        SDL_DestroyTexture(video_state->texture);
    }

    SDL_LockMutex(video_state->screen_mutex);
    // Allocate a place to put YUV image
    video_state->texture = SDL_CreateTexture(video_state->renderer,
                                             SDL_PIXELFORMAT_IYUV,
                                             SDL_TEXTUREACCESS_STREAMING,
                                             video_state->stream->codecpar->width,
                                             video_state->stream->codecpar->height);
    log_info("Created texture: %p", video_state->texture);

    SDL_UnlockMutex(video_state->screen_mutex);

    video_picture->width = video_state->stream->codecpar->width;
    video_picture->height = video_state->stream->codecpar->height;
    video_picture->allocated = 1;

    log_info("Allocated video picture: %dx%d", video_picture->width, video_picture->height);

    SDL_SetTextureBlendMode(video_state->texture, SDL_BLENDMODE_NONE);
}

int queue_picture(VideoState *video_state, AVFrame *frame, double presentation_time_stamp) {
    VideoPicture *video_picture;


    // Inorder to write to the queue, we need to wait for the buffer to clear out so we have space to store
    // the VideoPicture.

    SDL_LockMutex(video_state->picture_queue_mutex);
    while (video_state->picture_queue_size >= VIDEO_PICTURE_QUEUE_SIZE && !(*video_state->quit)) {
        log_warn("Picture queue full, waiting for space");
        SDL_CondWait(video_state->picture_queue_cond, video_state->picture_queue_mutex);
    }
    SDL_UnlockMutex(video_state->picture_queue_mutex);


    if (*video_state->quit) {
        log_warn("Video state quit, not queuing picture");
        return -1;
    }

    video_picture = &video_state->picture_queue[video_state->picture_queue_write_index];

    // allocate or resize the buffer
    if (!video_state->texture ||
        video_picture->width != video_state->codec_context->width ||
        video_picture->height != video_state->codec_context->height) {
        log_info("Allocating new video picture buffer");
        video_picture->allocated = 0;
        alloc_picture(video_state);
        if (*video_state->quit) {
            log_warn("Video state quit, not queuing picture");
            return -1;
        }
    }

    // Now we convert the image into YUV format that SDL can use
    if (video_state->texture) {
        void *pixels;
        int pitch;

        video_picture->presentation_time_stamp = presentation_time_stamp;

        if (SDL_LockTexture(video_state->texture, NULL, &pixels, &pitch) < 0) {
            log_error("Could not lock texture");
            return -1;
        }
        // Prepare destination planes (YUV format)
        // Since YUV420P uses 3 channels , 3 planes hav to be set
        uint8_t *dst_planes[3];
        dst_planes[0] = pixels; // Y plane
        dst_planes[1] = pixels + video_state->codec_context->height * pitch; // U plane
        dst_planes[2] = dst_planes[1] + (video_state->codec_context->height * pitch / 4); // V plane

        int dst_linesize[3] = {pitch, pitch / 2, pitch / 2};

        // Convert the image into YUV format that SDL uses
        sws_scale(video_state->sws_ctx,
                  (uint8_t const * const *) frame->data,
                  frame->linesize,
                  0,
                  frame->height,
                  dst_planes,
                  dst_linesize);
        log_info("Converted image to YUV format");

        SDL_UnlockTexture(video_state->texture);

        if (++video_state->picture_queue_write_index == VIDEO_PICTURE_QUEUE_SIZE) {
            log_info("Resetting picture queue write index");
            video_state->picture_queue_write_index = 0;
        }
        SDL_LockMutex(video_state->picture_queue_mutex);
        video_state->picture_queue_size++;
        log_info("Incremented picture queue size: %d", video_state->picture_queue_size);
        SDL_UnlockMutex(video_state->picture_queue_mutex);
    }
    return 0;
}

int video_thread(void *userdata) {
    PlayerState *player_state = (PlayerState *) userdata;
    VideoState *video_state = player_state->video_state;
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    double presentation_time_stamp; // Tells us when the video should be displayed

    while (true) {
        if (packet_queue_get(video_state->packet_queue, packet, 1) < 0) {
            log_warn("Nothing in the video queue");
            break;
        }
        //send packet for decoding
        if (avcodec_send_packet(video_state->codec_context, packet) < 0) {
            log_error("Failed to send packet for decoding");
            av_packet_unref(packet);
            continue;
        }
        log_info("Sent video packet for decoding");

        // Get decoded frame.
        if (avcodec_receive_frame(video_state->codec_context, frame) < 0) {
            log_error("Failed to get a frame");
            // TODO: What do we do when we fail to get a frame???
            // Just continue for now and hope everything works :D
            continue;
        }

        if (packet->dts == AV_NOPTS_VALUE) {
            log_warn("Undefined DTS value, using best effort");
            presentation_time_stamp = frame->best_effort_timestamp;
        } else {
            presentation_time_stamp = packet->dts;
        }

        presentation_time_stamp *= av_q2d(video_state->stream->time_base);
        log_info("Got video frame: presentation_time_stamp=%f", presentation_time_stamp);

        presentation_time_stamp = synchronize_video(video_state, frame, presentation_time_stamp);
        if (queue_picture(video_state, frame, presentation_time_stamp) < 0) {
            log_error("Failed to queue picture");
            break;
        }
        log_info("Queued picture");

        av_packet_unref(packet);
    }

    av_free(frame);

    av_free(packet);

    return
            0;
}

void video_display(VideoState *video_state) {
    if (!video_state || !video_state->texture || !video_state->stream) {
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
    log_info("Cleared renderer");
    SDL_RenderCopy(video_state->renderer, video_state->texture, NULL, &rect);
    log_info("Copied texture to renderer");
    SDL_RenderPresent(video_state->renderer);
    log_info("Presented renderer");
    SDL_UnlockMutex(video_state->screen_mutex);
}

static int find_stream_index(VideoState *video_state, AVFormatContext *format_context) {
    video_state->stream_index = -1;
    video_state->stream_index = av_find_best_stream(format_context,
                                                    AVMEDIA_TYPE_VIDEO,
                                                    AVMEDIA_TYPE_VIDEO,
                                                    -1,
                                                    NULL,
                                                    0);

    if (video_state->stream_index == -1) {
        log_error("Could not find video stream");
        return -1;
    }
    log_info("Found video stream at %d", video_state->stream_index);

    return 0;
}

int stream_component_open(VideoState *video_state, AVFormatContext *format_context) {
    int ret = 0;
    const AVCodec *codec = avcodec_find_decoder(format_context->streams[video_state->stream_index]->codecpar->codec_id);
    AVCodecContext *codec_ctx;
    if (!codec) {
        log_error("Could not find codec");
        return -1;
    }
    log_info("Found video codec: %s", codec->name);

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        log_error("Could not allocate codec context");
        ret = -1;
        goto cleanup;
    }
    log_info("Allocated video codec context");

    if (avcodec_parameters_to_context(codec_ctx, format_context->streams[video_state->stream_index]->codecpar) < 0) {
        log_error("Could not copy codec context");
        avcodec_free_context(&codec_ctx);
        ret = -1;
        goto cleanup;
    }

    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        log_error("Could not open codec");
        ret = -1;
        goto cleanup;
    }
    log_info("Opened video codec");

    video_state->stream = format_context->streams[video_state->stream_index];
    video_state->codec_context = codec_ctx;
    video_state->sws_ctx = sws_getContext(video_state->codec_context->width,
                                          video_state->codec_context->height,
                                          video_state->codec_context->pix_fmt,
                                          video_state->codec_context->width,
                                          video_state->codec_context->height,
                                          AV_PIX_FMT_YUV420P,
                                          SWS_BILINEAR, NULL, NULL, NULL
    );

    if (!video_state->sws_ctx) {
        log_error("Could not create SWS context");
        ret = -1;
        goto cleanup;
    }

    video_state->frame_timer = (double) av_gettime() / 1000000.0;
    video_state->frame_last_delay = 40e-3;
    video_state->video_current_pts_time = av_gettime();

    return ret;
cleanup:
    if (codec_ctx) {
        avcodec_free_context(&codec_ctx);
    }
    if (video_state->sws_ctx) {
        sws_freeContext(video_state->sws_ctx);
    }

    return ret;
}

int video_init(VideoState *video_state, PlayerState *player_state, SDL_Renderer *renderer) {
    if (find_stream_index(video_state, player_state->format_context) < 0) {
        log_error("Could not find stream info");
        return -1;
    }
    video_state->renderer = renderer;
    video_state->texture = NULL;
    video_state->screen_mutex = SDL_CreateMutex();
    video_state->picture_queue_mutex = SDL_CreateMutex();
    video_state->picture_queue_cond = SDL_CreateCond();
    video_state->picture_queue_size = 0;
    video_state->picture_queue_read_index = 0;
    video_state->picture_queue_write_index = 0;

    if (stream_component_open(video_state, player_state->format_context) < 0) {
        log_error("Could not open video stream component");
        return -1;
    }
    video_state->packet_queue = malloc(sizeof(PacketQueue));

    if (packet_queue_init(video_state->packet_queue, "Video Queue") < 0) {
        return -1;
    };

    set_get_audio_clock_fn((GetAudioClockFn) get_audio_clock, video_state, player_state->audio_state);

    return 0;
}

int video_state_reset(VideoState *video_state) {
    // TODO: What about the pictures queues?
    video_state->frame_timer = (double) av_gettime() / 1000000.0;
    video_state->frame_last_delay = 40e-3;
    video_state->video_current_pts = NAN;
    video_state->video_current_pts_time = av_gettime();
}

void video_cleanup(VideoState *video_state) {
    if (video_state->texture) {
        SDL_DestroyTexture(video_state->texture);
        log_info("Video texture destroyed");
    }

    if (video_state->sws_ctx) {
        sws_freeContext(video_state->sws_ctx);
        log_info("SWS context destroyed");
    }

    if (video_state->codec_context) {
        avcodec_free_context(&video_state->codec_context);
        log_info("Codec context destroyed");
    }

    if (video_state->screen_mutex) {
        SDL_DestroyMutex(video_state->screen_mutex);
        log_info("Screen mutex destroyed");
    }

    if (video_state->picture_queue_mutex) {
        SDL_DestroyMutex(video_state->picture_queue_mutex);
        log_info("Picture queue mutex destroyed");
    }

    if (video_state->picture_queue_cond) {
        SDL_DestroyCond(video_state->picture_queue_cond);
        log_info("Picture queue condition destroyed");
    }

    if (video_state->packet_queue) {
        packet_queue_destroy(video_state->packet_queue);
        log_info("Packet queue destroyed");
    }
}
