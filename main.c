#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include "decoders/video_decoder.h"
#include "libs/microlog/microlog.h"
#include <SDL.h>
#include <SDL_thread.h>

void save_frames_as_ppm(AVFrame *frame, void *user_data) {
    static int frame_count = 0;
    char filename[32];


    sprintf(filename, "../out/ppm/frame%d.ppm", frame_count++);
    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        log_error("Could not open file %s", filename);
        return;
    }

    // Write header
    fprintf(file, "P6\n%d %d\n255\n", frame->width, frame->height);
    log_info("Wrote header for frame %d", frame_count);

    // Write pixel data
    for (int y = 0; y < frame->height; y++) {
        fwrite(frame->data[0] + y * frame->linesize[0], 1, frame->width * 3, file);
    }

    log_info("Saved frame %d to %s", frame_count, filename);
    fclose(file);
}

int main(void) {
    const char *URL = "../data/videos/test.mp4";

    VideoDecoder *decoder = video_decoder_init(URL);
    if (!decoder) {
        log_error("Could not initialize video decoder");
        return -1;
    }
    // Dump format information
    av_dump_format(decoder->fmt_ctx, 0, URL, 0);

    if (decode(decoder, save_frames_as_ppm, NULL)) {
        log_error("Decoding failed");
        video_decoder_destroy(decoder);
        return -1;
    }

    video_decoder_destroy(decoder);
    return 0;
}
