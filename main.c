#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include "decoders/video_decoder.h"
#include "libs/microlog/microlog.h"

int main(void) {
    const char *URL = "../data/videos/test.mp4";

    VideoDecoder *decoder = video_decoder_init(URL);
    if (!decoder) {
        log_error("Could not initialize video decoder");
        return -1;
    }
    // Dump format information
    av_dump_format(decoder->fmt_ctx, 0, URL, 0);

    // Clean up
    video_decoder_destroy(decoder);
}
