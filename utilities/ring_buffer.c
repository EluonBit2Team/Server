#include "ring_buffer.h"
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>


void ring_init( ring_t *ring){
  ring->tag_head = ring->tag_tail = 0; // 태그 값을 0으로 초기화
}

void ring_put(ring_t *ring, int fd) {
    // ring에 데이터 저장
    ssize_t bytes_read = read(fd, ring->item[ring->tag_head].data, MAX_RING_DATA_SIZE);
    if (bytes_read == -1) {
        perror("Error reading from file descriptor");
        return;
    }
    ring->item[ring->tag_head].sz_data = bytes_read;

    // ring tag 조정
    ring->tag_head = (ring->tag_head + 1) % MAX_RING_SIZE; // head 증가
    if (ring->tag_head == ring->tag_tail) { // 버퍼가 모두 찼다면
        ring->tag_tail = (ring->tag_tail + 1) % MAX_RING_SIZE; // tail 증가
    }
}


int ring_get(ring_t *ring, int fd) {
    // 큐에 데이터가 없다면 복귀
    if (ring->tag_head == ring->tag_tail) {
        return 0; // 테이터 없음
    }

    // 큐 데이터 구하기
    int sz_data = ring->item[ring->tag_tail].sz_data;

    ssize_t bytes_written = read(fd, ring->item[ring->tag_tail].data, sz_data);
    if (bytes_written == -1) {
        perror("Error writing to file descriptor");
        return -1;
    }

    ring->tag_tail = (ring->tag_tail + 1) % MAX_RING_SIZE;  // tail 증가

    return bytes_written;
}