//
// Created by Deshy on 2025/05/14.
//
#include "packet_queue.h"

#include <SDL_timer.h>

#include "../libs/microlog/microlog.h"
#include "../player/player.h"
#include <stdbool.h>
#include "../audio/audio.h"
#include "../video/video.h"

#define MAX_AUDIO_QUEUE_SIZE (10 * 1024 * 1024)
#define MAX_VIDEO_QUEUE_SIZE (5 * 256 * 1024)


int packet_queue_put(PacketQueue *queue, AVPacket *packet) {
    AVPacket *pkt_copy = av_packet_alloc();

    if (!pkt_copy) {
        log_error("[%s]Failed to allocate packet copy", queue->name);
        return -1;
    }

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
    log_info("[%s] Packet queued: %d packets, size: %d", queue->name, queue->nb_packets, queue->size);
    return 0;
}

int packet_queue_init(PacketQueue *queue, char *name) {
    queue->packet_fifo = av_fifo_alloc2(32, sizeof(AVPacket *), AV_FIFO_FLAG_AUTO_GROW);
    queue->mutex = SDL_CreateMutex();
    if (!queue->mutex) {
        log_error("Failed to create %s mutex: %s", name, SDL_GetError());
        return -1;
    }
    queue->cond = SDL_CreateCond();
    if (!queue->cond) {
        log_error("Failed to create %s condition variable: %s", name, SDL_GetError());
        return -1;
    }
    queue->name = name;
    log_info("[%s] Packet queue initialized", name);
    return 0;
}

int packet_queue_get(PacketQueue *queue, AVPacket *packet, int block) {
    AVPacket *pkt = av_packet_alloc();
    int ret = 0;

    SDL_LockMutex(queue->mutex);
    log_debug("[%s] Locked packet queue mutex, waiting for packet...", queue->name);
    while (true) {
        if (av_fifo_read(queue->packet_fifo, &pkt, 1) >= 0) {
            log_debug("[%s] Got packet from queue", queue->name);
            queue->nb_packets--;
            queue->size -= pkt->size;
            av_packet_move_ref(packet, pkt);
            av_packet_free(&pkt);
            ret = 1;
            break;
        } else if (!block) {
            log_debug("[%s] No packet available and not blocking", queue->name);
            ret = 0;
            break;
        } else {
            log_warn("[%s] Waiting for packet in queue...", queue->name);
            SDL_CondWait(queue->cond, queue->mutex);
        }
    }
    SDL_UnlockMutex(queue->mutex);
    log_info("[%s] Packet dequeued: %d packets, size: %d", queue->name, queue->nb_packets, queue->size);
    return ret;
}

void packet_queue_flush(PacketQueue *queue) {
    SDL_LockMutex(queue->mutex);

    AVPacket *pkt;
    while (av_fifo_read(queue->packet_fifo, &pkt, 1) >= 0) {
        av_packet_free(&pkt);
    }
    queue->nb_packets = 0;
    queue->size = 0;
    SDL_UnlockMutex(queue->mutex);
    log_info("[%s] Packet queue flushed", queue->name);
}

void packet_queue_destroy(PacketQueue *queue) {
    packet_queue_flush(queue);
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
        wait_if_paused();

        if (*player_state->quit) {
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
        log_debug("Read packet: stream_index=%d, size=%d", packet->stream_index, packet->size);

        if (packet->stream_index == player_state->video_state->stream_index) {
            packet_queue_put(player_state->video_packet_queue, packet);
            log_info("Added video packet to video queue");
        } else if (packet->stream_index == player_state->audio_state->stream_index) {
            packet_queue_put(player_state->audio_packet_queue, packet);
            log_info("Added audio packet to audio queue");
        }
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    return 0;
}
