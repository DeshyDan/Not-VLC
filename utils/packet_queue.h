//
// Created by Deshy on 2025/05/14.
//

#ifndef PACKET_QUEUE_H
#define PACKET_QUEUE_H

#include <SDL_mutex.h>
#include <libavcodec/avcodec.h>
#include <libavutil/fifo.h>

typedef struct PacketQueue {
    char *name;
    AVFifo *packet_fifo;
    int nb_packets;
    int size; // total size of packets in bytes
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

void packet_queue_init(PacketQueue *queue, char *name);
void packet_queue_destroy(PacketQueue *queue);
int packet_queue_put(PacketQueue *queue, AVPacket *packet);
int packet_queue_get(PacketQueue *queue, AVPacket *packet, int block);
int packet_queueing_thread(void *userdata);
#endif //PACKET_QUEUE_H
