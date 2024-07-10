GCC = gcc
C_FLAGS = -std=c99 -D_POSIX_SOURCE
LINK_THREAD_FLAG = -lpthread
LINK_MARIA_FLAG = -I/usr/include/mysql -L/usr/lib64/mysql -lmysqlclient
TARGET = epoll_server.out

UTILS_DIR = utilities
UTILS_FILES = ring_buffer.c void_queue.c packet_converter.c
UTILS_OBJECT = $(patsubst %.c,$(UTILS_DIR)/%.o,$(UTILS_FILES))

CORE_DIR = cores
CORE_FILES = NetCore.c session.c uid_hash_map.c service.c
CORE_OBJECT = $(patsubst %.c,$(CORE_DIR)/%.o,$(CORE_FILES))

MAIN = main.c
MAIN_OBJECT = $(MAIN:.c=.o)

JSON_SUBDIR = cJSON
JSON_AR = $(JSON_SUBDIR)/cJSON.a

MARIADB_DIR = mariadb
MARIADB_FILES = mariadb_pool.c mariadb.c
MARIADB_OBJECT = $(patsubst %.c,$(MARIADB_DIR)/%.o,$(MARIADB_FILES))

SCHEDULER_MANAGE_DIR = scheduler_manage
SCHEDULER_MANAGE_FILES = child_process.c sig_handle.c
SCHEDULER_MANAGE_OBJECT = $(patsubst %.c,$(SCHEDULER_MANAGE_DIR)/%.o,$(SCHEDULER_MANAGE_FILES))

SCHEDULER_DIR = scheduler
SCHEDULER_TARGET = $(SCHEDULER_DIR)/server_scheduler

all : $(TARGET) $(SCHEDULER_TARGET)

$(TARGET) : $(CORE_OBJECT) $(UTILS_OBJECT) $(MAIN_OBJECT) $(MARIADB_OBJECT) $(SCHEDULER_MANAGE_OBJECT) $(JSON_AR)
	$(GCC) -o $@ $? $(LINK_THREAD_FLAG) $(LINK_MARIA_FLAG) 

$(JSON_AR) : 
	@make -C $(JSON_SUBDIR)

$(SCHEDULER_TARGET) :
	@make -C $(SCHEDULER_DIR)

%.o : %.c
	$(GCC) -o $@ -c $(C_FLAGS) $<

clean :
	rm -rf $(CORE_OBJECT) $(UTILS_OBJECT) $(MARIADB_OBJECT) $(MAIN_OBJECT) $(MARIADB_OBJECT) $(SCHEDULER_MANAGE_OBJECT)
fclean : clean
	rm -rf $(TARGET) $(SCHEDULER_TARGET)
re : clean fclean all

.PHONY : all clean fclean re