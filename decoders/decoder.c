#include "decoder.h"

#include <SDL_audio.h>
#include <SDL_events.h>
#include <SDL_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include "../libs/microlog/microlog.h"

#define MAX_AUDIO_QUEUE_SIZE (5 * 16 * 1024)
#define MAX_VIDEO_QUEUE_SIZE (5 * 256 * 1024)


int packet_queue_put(PacketQueue *queue, AVPacket *packet) {
    AVPacket *pkt_copy = av_packet_alloc();
    if (av_packet_ref(pkt_copy, packet) < 0) {
        av_packet_free(&pkt_copy);
        return -1;
    }

    SDL_LockMutex(queue->mutex);

    av_fifo_write(queue->packet_fifo, &pkt_copy, 1);
    queue->nb_packets++;
    queue->size += pkt_copy->size;

    SDL_CondSignal(queue->cond); // Wake up packet_queue_get()
    SDL_UnlockMutex(queue->mutex);
    log_info("Packet queued: %d packets, size: %d", queue->nb_packets, queue->size);
    return 0;
}

void packet_queue_init(PacketQueue *queue) {
    memset(queue, 0, sizeof(PacketQueue));
    queue->packet_fifo = av_fifo_alloc2(32, sizeof(AVPacket *), AV_FIFO_FLAG_AUTO_GROW);
    queue->mutex = SDL_CreateMutex();
    queue->cond = SDL_CreateCond();
    log_info("Packet queue initialized");
}

int packet_queue_get(PacketQueue *queue, AVPacket *packet, int block) {
    AVPacket *pkt = av_packet_alloc();
    int ret = 0;

    SDL_LockMutex(queue->mutex);
    while (true) {
        if (av_fifo_read(queue->packet_fifo, &pkt, 1) >= 0) {
            queue->nb_packets--;
            queue->size -= pkt->size;
            av_packet_move_ref(packet, pkt);
            av_packet_free(&pkt);
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            SDL_CondWait(queue->cond, queue->mutex);
        }
    }
    SDL_UnlockMutex(queue->mutex);
    log_info("Packet dequeued: %d packets, size: %d", queue->nb_packets, queue->size);
    return ret;
}

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

int find_streams(VideoState *video_state) {
    // Find video stream
    // TODO: Look at what related streams does in depth
    video_state->video_stream_index = av_find_best_stream(video_state->format_context,
                                                          AVMEDIA_TYPE_VIDEO,
                                                          AVMEDIA_TYPE_VIDEO,
                                                          -1,
                                                          NULL,
                                                          0);

    // AUDIO stream
    video_state->audio_stream_index = av_find_best_stream(video_state->format_context,
                                                          AVMEDIA_TYPE_AUDIO,
                                                          AVMEDIA_TYPE_AUDIO,
                                                          AVMEDIA_TYPE_VIDEO,
                                                          NULL,
                                                          0);

    if (video_state->audio_stream_index == -1 || video_state->video_stream_index == -1) {
        return -1;
    }

    log_info("Found video stream at %d", video_state->video_stream_index);
    log_info("Found audio stream at %d", video_state->audio_stream_index);

    return 0;
}

void alloc_picture(void *userdata) {
    VideoState *video_state = (VideoState *) userdata;
    VideoPicture *video_picture;

    video_picture = &video_state->picture_queue[video_state->picture_queue_write_index];

    if (video_state->video_texture) {
        SDL_DestroyTexture(video_state->video_texture);
    }

    SDL_LockMutex(video_state->screen_mutex);
    // Allocate a place to put YUV image
    video_state->video_texture = SDL_CreateTexture(video_state->renderer,
                                                   SDL_PIXELFORMAT_IYUV,
                                                   SDL_TEXTUREACCESS_STREAMING,
                                                   video_state->video_stream->codecpar->width,
                                                   video_state->video_stream->codecpar->height);

    SDL_UnlockMutex(video_state->screen_mutex);

    video_picture->width = video_state->video_stream->codecpar->width;
    video_picture->height = video_state->video_stream->codecpar->height;
    video_picture->allocated = 1;

    log_info("Allocated video picture: %dx%d", video_picture->width, video_picture->height);

    SDL_SetTextureBlendMode(video_state->video_texture, SDL_BLENDMODE_NONE);
}

int queue_picture(VideoState *video_state, AVFrame *frame) {
    VideoPicture *video_picture;

    // Inorder to write to the queue, we need to wait for the buffer to clear out so we have space to store
    // the VideoPicture.

    SDL_LockMutex(video_state->picture_queue_mutex);
    while (video_state->picture_queue_size >= VIDEO_PICTURE_QUEUE_SIZE && !video_state->quit) {
        log_warn("Picture queue full, waiting for space");
        SDL_CondWait(video_state->picture_queue_cond, video_state->picture_queue_mutex);
    }
    SDL_UnlockMutex(video_state->picture_queue_mutex);


    if (video_state->quit) {
        return -1;
    }

    video_picture = &video_state->picture_queue[video_state->picture_queue_write_index];

    // allocate or resize the buffer
    if (!video_state->video_texture ||
        video_picture->width != video_state->video_codec_ctx->width ||
        video_picture->height != video_state->video_codec_ctx->height) {
        log_info("Allocating new video picture buffer");
        video_picture->allocated = 0;
        alloc_picture(video_state);
        if (video_state->quit) {
            return -1;
        }
    }

    // Now we convert the image into YUV format that SDL can use
    if (video_state->video_texture) {
        void *pixels;
        int pitch;

        if (SDL_LockTexture(video_state->video_texture, NULL, &pixels, &pitch) < 0) {
            log_error("Could not lock texture");
            return -1;
        }
        // Prepare destination planes (YUV format)
        // Since YUV420P uses 3 channels , 3 planes hav to be set
        uint8_t *dst_planes[3];
        dst_planes[0] = pixels; // Y plane
        dst_planes[1] = pixels + video_state->video_codec_ctx->height * pitch; // U plane
        dst_planes[2] = dst_planes[1] + (video_state->video_codec_ctx->height * pitch / 4); // V plane

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

        SDL_UnlockTexture(video_state->video_texture);

        if (++video_state->picture_queue_write_index == VIDEO_PICTURE_QUEUE_SIZE) {
            video_state->picture_queue_write_index = 0;
        }
        SDL_LockMutex(video_state->picture_queue_mutex);
        video_state->picture_queue_size++;
        SDL_UnlockMutex(video_state->picture_queue_mutex);
    }
    return 0;
}

int video_thread(void *arg) {
    VideoState *video_state = (VideoState *) arg;
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    while (true) {
        if (packet_queue_get(&video_state->video_packet_queue, packet, 1) < 0) {
            break;
        }
        //send packet for decoding
        if (avcodec_send_packet(video_state->video_codec_ctx, packet) < 0) {
            log_error("Failed to send packet for decoding");
            av_packet_unref(packet);
            continue;
        }

        // Get decoded frame.
        if (avcodec_receive_frame(video_state->video_codec_ctx, frame) < 0) {
            log_error("Failed to get a frame");
            // TODO: What do we do when we fail to get a frame???
            // Just continue for now and hope everything works :D
            continue;
        } else {
            if (queue_picture(video_state, frame) < 0) {
                break;
            }
        }
        av_packet_unref(packet);
    }
    av_free(frame);
    av_free(packet);
    return 0;
}

int audio_decode_frame(VideoState *video_state) {
    int data_size = 0;
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    uint8_t *audio_buf = NULL;

    while (1) {
        if (packet_queue_get(&video_state->audio_packet_queue, packet, 1) <= 0) {
            log_warn("Nothing in the audio queue");
            av_frame_free(&frame);
            av_packet_free(&packet);
            return -1;
        }

        log_info("Sending packet for decoding");
        if (avcodec_send_packet(video_state->audio_codec_context, packet) < 0) {
            log_warn("Failed to send packet for decoding");
            av_packet_unref(packet);
            continue;
        }


        log_info("Receiving frame from decoder");
        if (avcodec_receive_frame(video_state->audio_codec_context, frame) < 0) {
            log_error("Failed to receive frame");
            av_packet_unref(packet);
            continue;
        }

        // Resample audio to S16 format
        int out_samples = av_rescale_rnd(
            swr_get_delay(video_state->swr_ctx, frame->sample_rate) + frame->nb_samples,
            frame->sample_rate,
            frame->sample_rate,
            AV_ROUND_UP
        );
        av_samples_alloc(&audio_buf,
                         NULL,
                         frame->ch_layout.nb_channels,
                         out_samples,
                         AV_SAMPLE_FMT_S16,
                         0);

        int converted_samples = swr_convert(
            video_state->swr_ctx,
            &audio_buf,
            out_samples,
            (const uint8_t **) frame->data,
            frame->nb_samples
        );
        data_size = converted_samples * frame->ch_layout.nb_channels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);

        if (data_size > sizeof(video_state->audio_buffer)) {
            log_error("Audio buffer too small");
            av_freep(&audio_buf);
            av_frame_free(&frame);
            av_packet_free(&packet);
            return -1;
        }

        memcpy(video_state->audio_buffer, audio_buf, data_size);
        av_freep(&audio_buf);
        av_frame_free(&frame);
        av_packet_free(&packet);
        log_info("Decoded %d bytes of audio data", data_size);
        return data_size;
    }
}

void sdl_audio_callback(void *userdata, Uint8 *stream, int len) {
    VideoState *video_state = (VideoState *) userdata;
    int audio_size;
    int len1;

    if (!video_state->swr_ctx || !video_state->audio_codec_context) {
        memset(stream, 0, len); // Output silence if not ready
        log_warn("Audio context not ready");
        return;
    }
    while (len > 0) {
        if (video_state->audio_buffer_index >= video_state->audio_buffer_size) {
            audio_size = audio_decode_frame(video_state);
            if (audio_size < 0) {
                video_state->audio_buffer_size = 1024;
                memset(video_state->audio_buffer, 0, video_state->audio_buffer_size);
            } else {
                video_state->audio_buffer_size = audio_size;
            }
            video_state->audio_buffer_index = 0;
        }
        len1 = video_state->audio_buffer_size - video_state->audio_buffer_index;
        if (len1 > len) {
            len1 = len;
        }
        memcpy(stream, (uint8_t *) video_state->audio_buffer + video_state->audio_buffer_index, len1);
        len -= len1;
        stream += len1;
        video_state->audio_buffer_index += len1;
    }
}


/** We'll find our codec decoders, setup audio options and launch the audio and video decoding threads **/
int stream_component_open(VideoState *video_state, int stream_index) {
    if (stream_index < 0 || stream_index >= video_state->format_context->nb_streams) {
        return -1;
    }

    SDL_AudioSpec wanted_spec;
    SDL_AudioSpec spec;
    int ret = 0;

    const AVCodec *codec = avcodec_find_decoder(video_state->format_context->streams[stream_index]->codecpar->codec_id);

    if (!codec) {
        log_error("Could not find codec");
        return -1;
    }
    AVCodecContext *codec_context = avcodec_alloc_context3(codec);

    if (avcodec_parameters_to_context(codec_context, video_state->format_context->streams[stream_index]->codecpar) <
        0) {
        log_error("Could not copy codec context");
        ret = -1;
        goto cleanup;
    }
    log_info("Copied video codex context");
    if (avcodec_open2(codec_context, codec, NULL) < 0) {
        log_error("Failed to open decoder");
        ret = -1;
        goto cleanup;
    }
    switch (codec_context->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            wanted_spec.freq = codec_context->sample_rate;
            wanted_spec.format = AUDIO_S16SYS;
            wanted_spec.channels = codec_context->ch_layout.nb_channels;
            wanted_spec.silence = 0;
            wanted_spec.callback = sdl_audio_callback;
            wanted_spec.userdata = video_state;
            wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;

            video_state->swr_ctx = swr_alloc();

        // Convert audio to FMT_S16
            swr_alloc_set_opts2(
                &video_state->swr_ctx,
                // Output
                &codec_context->ch_layout, // output channel layout
                AV_SAMPLE_FMT_S16, // output sample format
                codec_context->sample_rate, // output sample rate
                // Input
                &codec_context->ch_layout, // input channel layout
                codec_context->sample_fmt, // input sample format
                codec_context->sample_rate, // input sample rate
                0,
                NULL
            );

            if (!video_state->swr_ctx) {
                log_error("swr_alloc_set_opts failed");
                ret = -1;
                goto cleanup;
            }
            if (swr_init(video_state->swr_ctx) < 0) {
                log_error("Failed to initialize the resampling context");
                swr_free(&video_state->swr_ctx);
                video_state->swr_ctx = NULL;
                ret = -1;
                goto cleanup;
            }


            if (SDL_OpenAudio(&wanted_spec, &spec) < 0) {
                log_error("Failed to open SDL audio");
                ret = -1;
                goto cleanup;
            }

            video_state->audio_stream = video_state->format_context->streams[stream_index];
            video_state->audio_codec_context = codec_context;
            video_state->audio_buffer_size = 0;
            video_state->audio_buffer_index = 0;

            memset(&video_state->audio_packet, 0, sizeof(video_state->audio_packet));

            packet_queue_init(&video_state->audio_packet_queue);
        // video_state->audio_thread = SDL_CreateThread(audio_thread, "audio_decoder", video_state);
            log_info("Initialized audio packet queue");
            SDL_PauseAudio(0);
            break;
        case AVMEDIA_TYPE_VIDEO:
            video_state->video_stream_index = stream_index;
            video_state->video_stream = video_state->format_context->streams[stream_index];
            video_state->video_codec_ctx = codec_context;
            packet_queue_init(&video_state->video_packet_queue);
            log_info("Initialized video packet queue");
            video_state->video_thread = SDL_CreateThread(video_thread, "video_decoder", video_state);
            video_state->sws_ctx = sws_getContext(video_state->video_codec_ctx->width,
                                                  video_state->video_codec_ctx->height,
                                                  video_state->video_codec_ctx->pix_fmt,
                                                  video_state->video_codec_ctx->width,
                                                  video_state->video_codec_ctx->height,
                                                  AV_PIX_FMT_YUV420P,
                                                  SWS_BILINEAR, NULL, NULL, NULL
            );

            break;
        default:
            log_error("Unsupported codec type");
            ret = -1;
            goto cleanup;
    }


    return ret;
cleanup:
    if (codec_context) {
        avcodec_free_context(&codec_context);
    }

    return ret;
}

void video_display(VideoState *video_state) {
    if (!video_state || !video_state->video_texture || !video_state->video_stream) {
        log_error("Invalid video state or missing components");
        return;
    }

    VideoPicture *vp = &video_state->picture_queue[video_state->picture_queue_read_index];
    if (!vp->allocated) {
        log_warn("No frame available to display");
        return;
    }

    // Calculate display aspect ratio
    AVRational sar = video_state->video_stream->codecpar->sample_aspect_ratio;
    float aspect_ratio = (float) vp->width / (float) vp->height;
    if (sar.num != 0) {
        aspect_ratio = av_q2d(sar) * vp->width / vp->height;
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

void video_refresh_timer(void *userdata) {
    VideoState *video_state = (VideoState *) userdata;
    VideoPicture *video_picture;

    if (video_state->video_stream) {
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

int decode(void *userdata) {
    int ret = 0;

    VideoState *video_state = (VideoState *) userdata;
    video_state->format_context = avformat_alloc_context();
    AVPacket *packet = av_packet_alloc();

    video_state->audio_stream_index = -1;
    video_state->video_stream_index = -1;

    // Opening the video and reading the video file. Info is stored in the AVFormatContext
    if (avformat_open_input(&video_state->format_context, video_state->URL, 0, NULL) < 0) {
        log_error("Could not open source file %s", video_state->URL);
        ret = -1;
        goto cleanup;
    }

    // Retrieve stream information
    if (avformat_find_stream_info(video_state->format_context, NULL) < 0) {
        log_error("Could not find stream information");
        ret = -1;
        goto cleanup;
    }

    // Let's see what is in there
    av_dump_format(video_state->format_context, 0, video_state->URL, 0);

    if (find_streams(video_state) < 0) {
        log_error("Could not find streams from files");
        ret = -1;
        goto cleanup;
    }

    stream_component_open(video_state, video_state->video_stream_index);
    log_info("Opened video stream");
    stream_component_open(video_state, video_state->audio_stream_index);
    log_info("Opened audio stream");

    if (video_state->audio_stream < 0 || video_state->video_stream < 0) {
        log_info("Could not initialize streams");
        ret = -11;
        goto cleanup;
    }

    while (true) {
        if (video_state->quit) {
            break;
        }

        if (video_state->audio_packet_queue.size > MAX_AUDIO_QUEUE_SIZE ||
            video_state->video_packet_queue.size > MAX_VIDEO_QUEUE_SIZE) {
            SDL_Delay(10);
            continue;
        }

        if (av_read_frame(video_state->format_context, packet) < 0) {
            if (video_state->format_context->pb->error == 0) {
                SDL_Delay(100);
                continue;
            } else {
                break;
            }
        }

        if (packet->stream_index == video_state->video_stream_index) {
            packet_queue_put(&video_state->video_packet_queue, packet);
            log_info("Added packet to video queue");
        } else if (packet->stream_index == video_state->audio_stream_index) {
            packet_queue_put(&video_state->audio_packet_queue, packet);
            log_info("Added packet to audio queue");
        } else {
            av_packet_free(&packet);
        }
    }


    return ret;

cleanup:
    if (video_state->format_context) {
        avformat_close_input(&video_state->format_context);
        log_info("Closed input streams");
    }
    if (1) {
        SDL_Event event;
        event.type = FF_QUIT_EVENT;
        event.user.data1 = video_state;
        SDL_PushEvent(&event);
    }
    return ret;
}
