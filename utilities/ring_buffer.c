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

void set_ring_header(ring_buf *ring)
{
    int tail_space = MAX_BUFF_SIZE - ring->front;

    if (HEADER_SIZE > tail_space) {
        memcpy(&ring->msg_size, &ring->buf[ring->front], tail_space);
        memcpy(((char*)&ring->msg_size) + tail_space, &ring->buf[0], HEADER_SIZE - tail_space);
    }
    else {
        memcpy(&ring->msg_size,&ring->buf[ring->front],HEADER_SIZE);       
    }
}

// 링 버퍼에서 데이터를 꺼내는 함수
bool ring_array(ring_buf *ring, char *data_ptr) {
    if (ring_empty(ring) || ring->msg_size < 0) {
        return false; 
    }

    set_ring_header(ring);
    if (ring->msg_size > get_ring_size(ring))
    {
        return false;
    }

    for (int i = 0; i < ring->msg_size; i++) {
        if (ring_empty(ring)) {
            return false; 
        }
        data_ptr[i] = ring_deque(ring);
    }
    return true;
}

int get_ring_size(ring_buf *ring) {
    int data_size;
    if (ring->rear > ring->front)
    {
        data_size = ring->rear - ring->front;
    }
    else{
        data_size = MAX_BUFF_SIZE - ring->front;
        data_size += ring->rear;
    }

    return data_size;
}
// 파일 디스크립터로부터 데이터를 읽어와 버퍼에 저장하는 함수
int ring_read(ring_buf *ring, int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl");
        return -1;
    }

    if (!(flags & O_NONBLOCK)) {
        printf("파일 디스크립터가 넌블로킹 모드가 아닙니다\n");
        return -1;
    }

    int bytes_read = 0;
    int ctrl = 0;
    int write_pos = 0;
    int read_len = 0;
    int tail_space = MAX_BUFF_SIZE - ring->rear;
    int avaliable_space = MAX_BUFF_SIZE - get_ring_size(ring);
    if (tail_space > avaliable_space)
    {
        tail_space = avaliable_space;
    }
    // 1번도는거
    if (ring->front > ring-> rear) {
        ctrl = 1;
        write_pos = (void*)ring->buf + ring->rear;
        read_len = get_ring_size(ring);
    }
    //2번도는거 
    else if (ring->rear > ring-> front) {
        ctrl = 2;
        write_pos = (void*)ring->buf + ring->rear;
        read_len = MAX_BUFF_SIZE - ring->rear;
    }
    else if (ring->front == ring->rear) {
        ring->front = 0;
        ring->rear = 0;
        ctrl = 1;
        write_pos = (void*)ring->buf;
        read_len = MAX_BUFF_SIZE;
    }
    for (int i=0;i<ctrl;i++)
    {
        // TODO: int강제형변환 수정
        int data_read = (int)recv(fd, write_pos, read_len, 0);
        if (data_read > 0) {
            bytes_read += data_read;
            ring->rear = (ring->rear + data_read) % MAX_BUFF_SIZE;
        } else if (data_read == -1) {
            perror("read error");
            return -1;
        }
        if(ctrl == 2) {
            write_pos = (void*)ring->buf;
            read_len = ring->front;
        }
    }
    
    set_ring_header(ring);
    return bytes_read;
}