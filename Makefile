GCC = gcc
FLAGS = -std=c99
TARGET = epoll_server.out

UTILS_DIR = utilities
UTILS_HEADER = ring_buffer.h void_queue.h
UTILS = $(patsubst %.h,$(UTILS_DIR)/%.c,$(UTILS_HEADER))
UTILS_OBJECT = $(UTILS:.c=.o)

CORE_DIR = cores
CORE_HEADER = NetCore.h session.h
CORE_SRC = $(patsubst %.h,$(CORE_DIR)/%.c,$(CORE_HEADER))
CORE_OBJECT = $(CORE_SRC:.c=.o)

MAIN = main.c
MAIN_OBJECT = $(MAIN:.c=.o)

JSON_SUBDIR = cJSON
JSON_AR = $(JSON_SUBDIR)/cJSON.a

all : $(TARGET)

$(TARGET) : $(CORE_OBJECT) $(UTILS_OBJECT) $(MAIN_OBJECT) $(JSON_AR)
	$(GCC) -o $@ -lpthread $?

$(JSON_AR) : 
	make -C $(JSON_SUBDIR)

%.o : %.c
	$(GCC) -o $@ -c $(FLAGS) $<

clean :
	rm -rf $(CORE_OBJECT) $(UTILS_OBJECT) $(MAIN_OBJECT)
fclean : clean
	rm -rf $(TARGET)
re : clean fclean all

.PHONY : all clean fclean re