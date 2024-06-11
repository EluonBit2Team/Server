#include "ring_buffer.h"


void ring_init(ring_buf *ring) {
    ring->front = 0;
    ring->rear = 0;
}


bool ring_full(ring_buf *ring) 
{
    return NEXT(ring->rear) == ring->front;
}

bool ring_empty(ring_buf *ring)
{
    return ring->front == ring->rear;
}

char ring_deque(ring_buf *ring)
{
    if (ring_empty(ring))
    {
        printf("버퍼가 비었음\n");
        return -1;
    }
    char data = ring->buf[ring->front];
    ring->front = NEXT(ring->front);
    return data;
}

void ring_msg_size(ring_buf *ring)
{
    int tail_space = MAX_BUFF_SIZE - ring->front;

    if (HEADER_SIZE > tail_space) {
        printf("2044이후\n");
        int ms;
        memcpy(&ring->msg_size, &ring->buf[ring->front], tail_space);
        memcpy(((char*)&ring->msg_size) + tail_space, &ring->buf[0], HEADER_SIZE - tail_space);
    }
    else {
        printf("2044이전\n");
        memcpy(&ring->msg_size,&ring->buf[ring->front],HEADER_SIZE);       
    }
}

// 링 버퍼에서 데이터를 꺼내는 함수
bool ring_array(ring_buf *ring, char *data_ptr, int length) {

    if (length <= 0 || ring_empty(ring)) {
        printf("empty\n");
        return false; 
    }
 
    ring_msg_size(ring);
    printf("data: ");
    for (int i = 0; i < length; i++) {
        if (ring_empty(ring)) {
            return false; 
        }
        data_ptr[i] = ring_deque(ring);
        printf("%c",data_ptr[i]);
    }
    printf("\nmsg_size %d\n",ring->msg_size);
    return true;
}

// 파일 디스크립터로부터 데이터를 읽어와 버퍼에 저장하는 함수
int ring_read(ring_buf *ring, int fd) {
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

    int bytes_read = 0;
    int tail_space = MAX_BUFF_SIZE - ring->rear;
    printf("1. %d\n",ring->rear);
    int first_read = read(fd, ring->buf + ring->rear, tail_space);
    printf("2. %d\n",ring->rear);
    if (first_read > 0) {
        bytes_read += first_read;
        ring->rear = (ring->rear + first_read) % MAX_BUFF_SIZE;
        printf("3. %d\n",ring->rear);
    } else if (first_read == -1) {
        perror("read error");
        return -1;
    }

    if (first_read == tail_space) {
        printf("4. %d\n",ring->rear);
        int second_read = read(fd, ring->buf, ring->front);
        printf("5. %d\n",ring->rear);
        if (second_read > 0) {
            bytes_read += second_read;
            ring->rear = second_read % MAX_BUFF_SIZE;
            printf("6. %d\n",ring->rear);
            if (ring_full(ring)) {
                ring->front = (ring->rear + 1) % MAX_BUFF_SIZE;
            }
        }else if (second_read == -1) {
            perror("read error");
            return -1;
        }
    }
    printf("bytes_read : %d\n",bytes_read);
    return bytes_read;
}