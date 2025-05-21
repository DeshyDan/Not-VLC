//
// Created by Deshy on 2025/05/20.
//

#ifndef SYNC_H
#define SYNC_H
#include <stdint.h>
#define DEFAULT_AV_SYNC_TYPE AV_SYNC_VIDEO_MASTER

// type declarations
typedef struct AudioState AudioState;
typedef struct VideoState VideoState;

enum {
    AV_SYNC_AUDIO_MASTER,
    AV_SYNC_VIDEO_MASTER,
    AV_SYNC_EXTERNAL_MASTER,
};

typedef struct SyncState {
    int av_sync_type;
    double audio_clock; // clock for audio
    double video_clock;

} SyncState;

extern SyncState *sync_state;

void sync_init(int sync_type);

void sync_cleanup();

int synchronize_audio(AudioState *audio_state, short *samples, int samples_size, double presentation_time_stamp);

double synchronize_video(VideoState *video_state, AVFrame *frame, double presentation_time_stamp);

double get_master_clock(void *userdata);

double get_external_clock();

double get_audio_clock(AudioState *audio_state);
#endif //SYNC_H
