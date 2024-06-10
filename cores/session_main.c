#include "session.h"

int main()
{
    session_pool_t sp;
    init_session_pool(&sp, 100);
    printf("init done\n");
    client_session_t* cs_ptr;
    client_session_t* cs_ptr2;
    client_session_t* cs_ptr3;
    cs_ptr = assign_session(&sp, 5);
    if (cs_ptr == NULL)
    {
        printf("what1?\n");
    }
    printf("assign done\n");
    cs_ptr2 = find_session_by_fd(&sp, 5);
    printf("find_session_by_fd done\n");
    if (cs_ptr2 == NULL)
    {
        printf("what2?\n");
    }
    printf("cs_ptr : %p, fd: %d\n", cs_ptr, cs_ptr->fd);
    printf("cs_ptr2 : %p, fd: %d\n", cs_ptr2, cs_ptr2->fd);
    if(release_session(&sp, cs_ptr) < 0)
    {
        printf("what?\n");
    }
    if(release_session(&sp, cs_ptr2) < 0)
    {
        printf("ok\n");
    }
    cs_ptr3 = assign_session(&sp, 7);
    printf("cs_ptr3 : %p, idx: %d\n", cs_ptr, cs_ptr->session_idx);
}