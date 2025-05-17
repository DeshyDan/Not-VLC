//
// Created by Deshy on 2025/05/14.
//
#include "packet_queue.h"
#include "../libs/microlog/microlog.h"
#include <stdbool.h>

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
