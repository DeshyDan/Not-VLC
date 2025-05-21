//
// Created by Deshy on 2025/05/20.
#include <libavutil/time.h>
#include "../libs/microlog/microlog.h"
#include "../player/player.h"
#include "sync.h"
#include "../video/video.h"
#include "../audio/audio.h"
#define AV_SYNC_THRESHOLD 0.03
#define AV_NOSYNC_THRESHOLD 1.0

#define SAMPLE_CORRECTION_PERCENT_MAX 10
#define AUDIO_DIFF_AVG_NB 20


SyncState *sync_state = NULL;

double get_external_clock() {
    return av_gettime() / 1000000.0;
}

/** takes time to move all the data from audio packet to buffer which means that the value in the audio clock could be
 * too far ahead **/
double get_audio_clock(AudioState *audio_state) {
    double pts = sync_state->audio_clock;
    int hw_buf_size = audio_state->buffer_size - audio_state->buffer_index;
    int bytes_per_second = audio_state->stream->codecpar->sample_rate *
                          audio_state->codec_context->ch_layout.nb_channels * 2;

    double adjustment = (double) hw_buf_size / bytes_per_second;
    double adjusted_pts = pts - adjustment;

    log_debug("Audio clock: pts=%.3f, buf_size=%d, adj=%.3f, final=%.3f",
        pts, hw_buf_size, adjustment, adjusted_pts);

    return adjusted_pts;
}

double get_video_clock(VideoState *video_state) {
    if (video_state->video_current_pts_time == 0) {
        return video_state->video_current_pts;
    }

    return video_state->video_current_pts +
           (av_gettime() - video_state->video_current_pts_time) / 1000000.0;
}

double get_master_clock(void *userdata) {
    if (!sync_state) return 0.0;

    switch (sync_state->av_sync_type) {
        case AV_SYNC_AUDIO_MASTER:
            return get_audio_clock((AudioState *) userdata);
        case AV_SYNC_VIDEO_MASTER:
            return get_video_clock((VideoState *) userdata);

        default:
            return get_external_clock();
    }
}

double synchronize_video(VideoState *video_state, AVFrame *frame, double presentation_time_stamp) {
    double frame_delay;

    if (presentation_time_stamp != 0) {
        log_info("Synchronizing video: presentation_time_stamp=%f", presentation_time_stamp);
        sync_state->video_clock = presentation_time_stamp;
    } else {
        presentation_time_stamp = sync_state->video_clock;
        log_info("No presentation_time_stamp, using video clock: %f", sync_state->video_clock);
    }

    frame_delay = av_q2d(video_state->stream->time_base); // duration of a frame in seconds
    frame_delay += frame->repeat_pict * (frame_delay * 0.5); //if frame was repeated
    log_info("Frame delay: %f", frame_delay);
    sync_state->video_clock += frame_delay;
    log_info("Updated video clock: %f", sync_state->video_clock);
    return presentation_time_stamp;
}

int synchronize_audio(AudioState *audio_state, short *samples, int samples_size, double presentation_time_stamp) {
    int n = 2 * audio_state->codec_context->ch_layout.nb_channels;
    double ref_clock;

    if (sync_state->av_sync_type != AV_SYNC_AUDIO_MASTER) {
        double diff;
        double avg_diff;
        int wanted_size;
        int min_size;
        int max_size;

        ref_clock = get_master_clock(audio_state);
        diff = get_audio_clock(audio_state) - ref_clock;

        if (diff < AV_NOSYNC_THRESHOLD) {
            audio_state->audio_diff_cum = diff + audio_state->audio_diff_avg_coef * audio_state->audio_diff_cum;

            if (audio_state->audio_diff_avg_count < AUDIO_DIFF_AVG_NB) {
                audio_state->audio_diff_avg_count++;
            } else {
                avg_diff = audio_state->audio_diff_cum * (1.0 - audio_state->audio_diff_avg_coef);

                if (fabs(avg_diff) >= audio_state->audio_diff_threshold) {
                }
                wanted_size = samples_size + (int) (diff * audio_state->stream->codecpar->sample_rate * n);
                min_size = samples_size * ((100 - SAMPLE_CORRECTION_PERCENT_MAX) / 100);

                if (wanted_size < min_size) {
                    wanted_size = min_size;
                } else if (wanted_size > max_size) {
                    wanted_size = max_size;
                }

                if (wanted_size < samples_size) {
                    samples_size = wanted_size;
                } else if (wanted_size > samples_size) {
                    uint8_t *samples_end;
                    uint8_t *q;
                    int nb;

                    nb = (samples_size - wanted_size);
                    samples_end = (uint8_t *) samples + samples_size - n;
                    q = samples_end + n;
                    while (nb > 0) {
                        memcpy(q, samples_end, n);
                        q += n;
                        nb -= n;
                    }
                    samples_size = wanted_size;
                }
            }
        } else {
            // differnce  is too big, we don't want to use it
            audio_state->audio_diff_avg_count = 0;
            audio_state->audio_diff_cum = 0;
        }
    }
    return samples_size;
}

void sync_init(int sync_type) {
    if (!sync_state) {
        sync_state = malloc(sizeof(SyncState));
        sync_state->av_sync_type = sync_type;
    }
}

void sync_cleanup() {
    if (sync_state) {
        free(sync_state);
        sync_state = NULL;
    }
}
