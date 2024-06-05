#include "ring_buffer.h"

// 초기화하는 함수
void ring_clear(ring_buf *ring)
{
    ring->front = 0;
    ring->rear = 0;
}

// 꽉 찼는지 확인하는 함수
bool ring_full(ring_buf *ring)
{
    return NEXT(ring->rear) == ring->front;
}

// 비었는지 확인하는 함수
bool ring_empty(ring_buf *ring)
{
    return ring->front == ring->rear;
}

// 데이터를 추가하는 함수
void ring_enque(ring_buf *ring, char data)
{
    if (ring_full(ring))
    {
        printf("큐가 꽉 찼음\n");
        return;
    }
    ring->buf[ring->rear] = data;
    ring->rear = NEXT(ring->rear); 
}

// 데이터를 제거하고 반환하는 함수
char ring_deque(ring_buf *ring)
{
    if (ring_empty(ring))
    {
        printf("큐가 비었음\n");
        return -1; // 에러 코드로 -1 반환
    }
    char data = ring->buf[ring->front];
    ring->front = NEXT(ring->front);
    return data;
}

bool ring_array(ring_buf *queue, char *data_ptr, int length) {
    if (length <= 0 || ring_empty(queue)) {
        return false; 
    }

    for (int i = 0; i < length; i++) {
        if (ring_empty(queue)) {
            return false; 
        }
        data_ptr[i] = ring_deque(queue);
    }
    return true;
}
// 파일 디스크립터로부터 데이터를 읽어와 큐에 저장하는 함수
int ring_read(ring_buf *ring, int fd) {
    // 파일 디스크립터의 플래그를 가져옴
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl");
        return -1;
    }

    // 넌블로킹 모드인지 확인
    if (!(flags & O_NONBLOCK)) {
        printf("파일 디스크립터가 넌블로킹 모드가 아닙니다\n");
        return -1;
    }

    char temp_buf[QUEUE_SIZE];
    int bytes_read = read(fd, temp_buf, QUEUE_SIZE);
    if (bytes_read > 0) {
        for (int i = 0; i < bytes_read; i++) {
            ring_enque(ring, temp_buf[i]);
        }
    } else if (bytes_read == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        printf("넌블로킹 소켓에서 읽을 데이터가 없습니다\n");
        return 0;
    } else if (bytes_read == -1) {
        perror("read error");
        return -1;
    }

    return bytes_read;
}