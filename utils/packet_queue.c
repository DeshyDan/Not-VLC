//
// Created by Deshy on 2025/05/14.
//
#include "packet_queue.h"

#include <SDL_timer.h>

#include "../libs/microlog/microlog.h"
#include "../player/player.h"
#include <stdbool.h>

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

void packet_queue_destroy(PacketQueue *queue) {
    if (queue->packet_fifo) {
        av_fifo_freep2(&queue->packet_fifo);
    }
    if (queue->mutex) {
        SDL_DestroyMutex(queue->mutex);
    }
    if (queue->cond) {
        SDL_DestroyCond(queue->cond);
    }

    free(queue);
}

int packet_queueing_thread(void *userdata) {
    PlayerState *player_state = (PlayerState *) userdata;
    AVPacket *packet = av_packet_alloc();

    while (true) {
        if (player_state->quit) {
            log_warn("Player quit, exiting packet queueing thread");
            break;
        }

        if (player_state->audio_packet_queue->size > MAX_AUDIO_QUEUE_SIZE ||
            player_state->video_packet_queue->size > MAX_VIDEO_QUEUE_SIZE) {
            SDL_Delay(10);
            continue;
        }

        if (av_read_frame(player_state->format_context, packet) < 0) {
            if (player_state->format_context->pb->error == 0) {
                SDL_Delay(100);
                continue;
            } else {
                break;
            }
        }
        log_info("Read packet: stream_index=%d, size=%d", packet->stream_index, packet->size);

        if (packet->stream_index == player_state->video_state->stream_index) {
            packet_queue_put(player_state->video_packet_queue, packet);
            log_info("Added packet to video queue");
        } else if (packet->stream_index == player_state->audio_state->stream_index) {
            packet_queue_put(player_state->audio_packet_queue, packet);
            log_info("Added packet to audio queue");
        } else {
            av_packet_unref(packet);
        }
    }

    av_packet_free(&packet);
    return 0;
}
