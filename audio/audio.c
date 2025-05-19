//
// Created by Deshy on 2025/05/14.
//

#include "audio.h"

#include <libswresample/swresample.h>

#include "../libs/microlog/microlog.h"
#include "../player/player.h"

/** takes time to move all the data from audio packet to buffer which means that the value in the audio clock could be
 * too far ahead **/
double get_audio_clock(AudioState *audio_state) {
    double presentation_time_stamp = audio_state->audio_clock; // last known presentation time stamp
    int hw_buf_size = audio_state->buffer_size - audio_state->buffer_index;
    int bytes_per_second = 0;
    int n = audio_state->codec_context->ch_layout.nb_channels * 2;

    if (audio_state->stream) {
        bytes_per_second = audio_state->stream->codecpar->sample_rate * n;
    }

    if (bytes_per_second) {
        presentation_time_stamp -= (double) hw_buf_size / bytes_per_second;
        // time it would take to play remaining audio
    }

    return presentation_time_stamp;
}

int audio_decode_frame(AudioState *audio_state) {
    int data_size = 0;
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    uint8_t *audio_buf = NULL;
    double presentation_time_stamp = 0;

    while (1) {
        if (packet_queue_get(audio_state->audio_packet_queue, packet, 1) <= 0) {
            log_warn("Nothing in the audio queue");
            av_frame_free(&frame);
            av_packet_free(&packet);
            return -1;
        }

        log_info("Sending packet for decoding");
        if (avcodec_send_packet(audio_state->codec_context, packet) < 0) {
            log_warn("Failed to send packet for decoding");
            av_packet_unref(packet);
            continue;
        }

        log_info("Receiving frame from decoder");
        if (avcodec_receive_frame(audio_state->codec_context, frame) < 0) {
            log_error("Failed to receive frame");
            av_frame_unref(frame);
            av_packet_unref(packet);
            continue;
        }

        if (packet->dts != AV_NOPTS_VALUE) {
            audio_state->audio_clock = av_q2d(audio_state->stream->time_base) * packet->dts;
        } else {
            log_warn("Undefined DTS value");
        }

        // Resample audio to S16 format
        int out_samples = av_rescale_rnd(
            swr_get_delay(audio_state->swr_ctx, frame->sample_rate) + frame->nb_samples,
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
            audio_state->swr_ctx,
            &audio_buf,
            out_samples,
            (const uint8_t **) frame->data,
            frame->nb_samples
        );
        data_size = converted_samples * frame->ch_layout.nb_channels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);

        if (data_size > sizeof(audio_state->audio_buffer)) {
            log_error("Audio buffer too small");
            av_freep(&audio_buf);
            av_frame_free(&frame);
            av_packet_free(&packet);
            return -1;
        }

        memcpy(audio_state->audio_buffer, audio_buf, data_size);
        presentation_time_stamp = audio_state->audio_clock;
        int n = 2 * audio_state->codec_context->ch_layout.nb_channels;
        audio_state->audio_clock += (double) data_size / (double) (n * audio_state->codec_context->sample_rate);
        av_freep(&audio_buf);
        av_frame_free(&frame);
        av_packet_free(&packet);
        log_info("Decoded %d bytes of audio data", data_size);
        return data_size;
    }
}

void sdl_audio_callback(void *userdata, Uint8 *stream, int len) {
    log_info("SDL audio callback called");
    AudioState *audio_state = (AudioState *) userdata;
    int audio_size;
    int len1;

    if (!audio_state->swr_ctx || !audio_state->codec_context) {
        memset(stream, 0, len); // Output silence if not ready
        log_warn("Audio context not ready");
        return;
    }
    while (len > 0) {
        if (audio_state->buffer_index >= audio_state->buffer_size) {
            audio_size =  audio_decode_frame(audio_state);
            if (audio_size < 0) {
                log_info("Audio buffer empty, filling with silence");
                audio_state->buffer_size = 1024;
                memset(audio_state->audio_buffer, 0, audio_state->buffer_size);
            } else {
                log_info("Audio buffer filled with %d bytes", audio_size);
                audio_state->buffer_size = audio_size;
            }
            audio_state->buffer_index = 0;
        }

        log_info("Audio buffer index: %d, size: %d", audio_state->buffer_index, audio_state->buffer_size);
        len1 = audio_state->buffer_size - audio_state->buffer_index;
        if (len1 > len) {
            len1 = len;
        }
        memcpy(stream, (uint8_t *) audio_state->audio_buffer + audio_state->buffer_index, len1);
        len -= len1;
        stream += len1;
        audio_state->buffer_index += len1;
        log_info("Audio buffer index after copy: %d", audio_state->buffer_index);
    }
}

static int stream_component_open(AudioState *audio_state, AVFormatContext *format_context) {
    SDL_AudioSpec wanted_spec;
    SDL_AudioSpec spec;
    int ret = 0;


    const AVCodec *codec = avcodec_find_decoder(format_context->streams[audio_state->stream_index]->codecpar->codec_id);
    if (!codec) {
        log_error("Could not find codec");
        return -1;
    }
    log_info("Found audio codec: %s", codec->name);

    AVCodecContext *codec_context = avcodec_alloc_context3(codec);
    if (!codec_context) {
        log_error("Could not allocate codec context");
        ret = -1;
        goto cleanup;
    }
    log_info("Allocated audio codec context");

    if (avcodec_parameters_to_context(codec_context, format_context->streams[audio_state->stream_index]->codecpar) <
        0) {
        log_error("Could not copy codec context");
        ret = -1;
        goto cleanup;
    }

    if (avcodec_open2(codec_context, codec, NULL) < 0) {
        log_error("Failed to open decoder");
        ret = -1;
        goto cleanup;
    }
    log_info("Opened audio decoder");

    wanted_spec.freq = codec_context->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = codec_context->ch_layout.nb_channels;
    wanted_spec.silence = 0;
    wanted_spec.callback = sdl_audio_callback;
    wanted_spec.userdata = audio_state;
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;

    audio_state->swr_ctx = swr_alloc();

    // Convert audio to FMT_S16
    swr_alloc_set_opts2(
        &audio_state->swr_ctx,
        &codec_context->ch_layout, // output channel layout
        AV_SAMPLE_FMT_S16, // output sample format
        codec_context->sample_rate, // output sample rate
        &codec_context->ch_layout, // input channel layout
        codec_context->sample_fmt, // input sample format
        codec_context->sample_rate, // input sample rate
        0,
        NULL
    );

    if (!audio_state->swr_ctx) {
        log_error("swr_alloc_set_opts failed");
        ret = -1;
        goto cleanup;
    }
    log_info("Allocated audio resampling context");

    if (swr_init(audio_state->swr_ctx) < 0) {
        log_error("Failed to initialize the resampling context");
        swr_free(&audio_state->swr_ctx);
        audio_state->swr_ctx = NULL;
        ret = -1;
        goto cleanup;
    }
    log_info("Initialized audio resampling context");


    if (SDL_OpenAudio(&wanted_spec, &spec) < 0) {
        log_error("Failed to open SDL audio");
        ret = -1;
        goto cleanup;
    }
    log_info("Opened SDL audio device");

    audio_state->stream = format_context->streams[audio_state->stream_index];
    audio_state->codec_context = codec_context;
    audio_state->format_context = format_context;
    audio_state->buffer_size = 0;
    audio_state->buffer_index = 0;
    audio_state->packet = (AVPacket){0};

    SDL_PauseAudio(0);

    return ret;

cleanup:
    if (codec_context) {
        avcodec_free_context(&codec_context);
        log_info("Audio codec context freed");
    }
    if (audio_state->swr_ctx) {
        swr_free(&audio_state->swr_ctx);
        log_info("Audio resampling context freed");
    }

    return ret;
}

static int find_stream_index(AudioState *audio_state, AVFormatContext *fmt_ctx) {
    audio_state->stream_index = -1;
    audio_state->stream_index = av_find_best_stream(fmt_ctx,
                                                    AVMEDIA_TYPE_AUDIO,
                                                    AVMEDIA_TYPE_AUDIO,
                                                    AVMEDIA_TYPE_VIDEO,
                                                    NULL,
                                                    0);

    if (audio_state->stream_index == -1) {
        log_error("Could not find audio stream");
        return -1;
    }
    log_info("Found audio stream at %d", audio_state->stream_index);
    return 0;
}

int audio_init(AudioState *audio_state, PlayerState *player_state) {
    if (find_stream_index(audio_state, player_state->format_context) < 0) {
        log_error("Could not find audio stream index");
        return -1;
    }

    if (stream_component_open(audio_state, player_state->format_context) < 0) {
        log_error("Could not open audio stream");
        return -1;
    }
    audio_state->audio_packet_queue = malloc(sizeof(PacketQueue));
    packet_queue_init(audio_state->audio_packet_queue, "Audio Queue");
    log_info("Initialized audio packet queue");

    return 0;
}

void audio_cleanup(AudioState *audio_state) {
    if (audio_state->swr_ctx) {
        swr_free(&audio_state->swr_ctx);
        log_info("Audio resampling context freed");
    }
    if (audio_state->codec_context) {
        avcodec_free_context(&audio_state->codec_context);
        log_info("Audio codec context freed");
    }
    if (audio_state->format_context) {
        avformat_close_input(&audio_state->format_context);
        log_info("Audio format context closed");
    }
    if (audio_state->audio_packet_queue) {
        packet_queue_destroy(audio_state->audio_packet_queue);
        log_info("Audio packet queue destroyed");
    }
    if (audio_state->audio_packet_queue) {
        free(audio_state->audio_packet_queue);
        log_info("Audio packet queue freed");
    }

    SDL_CloseAudio();
    log_info("SDL audio device closed");
}
