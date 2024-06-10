#include "ring_buffer.h"

// 링 버퍼의 사이즈를 설정 하는 함수
void ring_init(ring_buf *ring) {
    ring->size = MIN_BUFF_SIZE;
    ring->buf = (char *)malloc(ring->size * sizeof(char));
    ring_clear(ring);
}

void ring_resize(ring_buf *ring, int data_size) {
    if (data_size <= MIN_BUFF_SIZE) {
        ring->size = MIN_BUFF_SIZE;
    } 
    else {
        ring->size = MAX_BUFF_SIZE;
    }
    printf("버퍼사이즈 %d로 설정\n",ring->size);

    if (ring->buf != NULL) {
        free(ring->buf);
    }

    ring->buf = (char *)malloc(ring->size * sizeof(char));
    ring_clear(ring);
}
// 초기화하는 함수
void ring_clear(ring_buf *ring) 
{
    ring->front = 0;
    ring->rear = 0;
}

// 꽉 찼는지 확인하는 함수
bool ring_full(ring_buf *ring) 
{
    return NEXT(ring->rear, ring->size) == ring->front;
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
    ring->rear = NEXT(ring->rear, ring->size); 
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
    ring->front = NEXT(ring->front, ring->size);
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
    int tail_space = ring->size - ring->rear;

    int first_read = read(fd, ring->buf + ring->rear, tail_space);
    printf("%d\n",tail_space);
    printf("%d\n",ring->rear);
    printf("%d\n",first_read);
    if (first_read > 0) {
        bytes_read += first_read;
        ring->rear = (ring->rear + first_read) % ring->size;
    } else if (first_read == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        printf("넌블로킹 소켓에서 읽을 데이터가 없습니다\n");
        return 0;
    } else if (first_read == -1) {
        perror("read error");
        return -1;
    }

    if (first_read == tail_space) {
        int second_read = read(fd, ring->buf, ring->front);
        if (second_read > 0) {
            bytes_read += second_read;
            ring->rear = second_read % ring->size;
        } else if (second_read == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            printf("넌블로킹 소켓에서 읽을 데이터가 없습니다\n");
            return bytes_read;
        } else if (second_read == -1) {
            perror("read error");
            return -1;
        }
    }

    // 링 버퍼가 가득 찬 경우 front를 이동하여 덮어쓰기 처리
    if (ring_full(ring)) {
        ring->front = (ring->rear + 1) % ring->size;
    }

    return bytes_read;
}
