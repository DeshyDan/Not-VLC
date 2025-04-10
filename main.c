#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include "decoders/video_decoder.h"
#include "libs/microlog/microlog.h"

void SaveFrame(AVFrame *frame, int width, int height, int iFrame) {
    char szFilename[32];

    sprintf(szFilename, "frame%d.ppm", iFrame);
    FILE *file = fopen(szFilename, "wb");
    if (file == NULL) {
        log_error("Could not open file %s", szFilename);
        return;
    }

    // Write header
    fprintf(file, "P6\n%d %d\n255\n", width, height);
    log_info("Wrote header for frame %d", iFrame);

    // Write pixel data
    for (int y = 0; y < height; y++) {
        fwrite(frame->data[0] + y * frame->linesize[0], 1, width * 3, file);
        log_info("Wrote pixel data for frame %d", iFrame);
    }

    log_info("Saved frame %d to %s", iFrame, szFilename);
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

    AVFrame *frame = av_frame_alloc();
    AVFrame *frame_rgb = av_frame_alloc();

    uint8_t *buffer = NULL;

    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_NV12, decoder->codec_ctx->width, decoder->codec_ctx->height, 1);
    log_info("%d bytes are needed for RGB frame", numBytes);
    buffer = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));
    if (av_image_fill_arrays(&frame_rgb->data, &frame_rgb->linesize, buffer, AV_PIX_FMT_RGB24,
                             decoder->codec_ctx->width, decoder->codec_ctx->height, 1) < 0) {
        log_error("Could not fill image arrays");
        return -1;
    }

    // handles image scaling and pixel format conversion
    struct SwsContext *sws_ctx = sws_getContext(decoder->codec_ctx->width,
                                                decoder->codec_ctx->height,
                                                decoder->codec_ctx->pix_fmt,
                                                decoder->codec_ctx->width,
                                                decoder->codec_ctx->height,
                                                AV_PIX_FMT_NV12,
                                                SWS_BILINEAR, NULL, NULL, NULL);
    log_info("Created sws context");

    // Packets aare pieces of data that can contain bits of data into raw frames that can be manipulated
    AVPacket *packet = av_packet_alloc();
    int i = 0;
    while (av_read_frame(decoder->fmt_ctx, packet) >= 0) {
        if (packet->stream_index == decoder->video_stream_index) {
        log_info("Got video packet with size %d", packet->size);
            // decode video frame
            if (avcodec_send_packet(decoder->codec_ctx, packet) < 0) {
                log_error("Failed to send packet for decoding");
                continue;
            } else {
                log_info("Decoded video packet");
                sws_scale(sws_ctx, (uint8_t const * const *) frame->data, frame->linesize, 0,
                          decoder->codec_ctx->height, frame_rgb->data, frame_rgb->linesize);

                // Save the frame to disk
                if (++i <= 5) {
                    SaveFrame(frame_rgb, decoder->codec_ctx->width,
                              decoder->codec_ctx->height, i);
                }
            }
        }
        av_packet_free(&packet);
    }


    // Clean up
    av_free(&buffer);
    av_frame_free(&frame);
    av_frame_free(&frame_rgb);
    video_decoder_destroy(decoder);
}
