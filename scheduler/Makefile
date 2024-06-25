GCC = gcc
C_FLAGS = -std=c99
LINK_MARIA_FLAG = -I/usr/include/mysql -L/usr/lib64/mysql -lmysqlclient

TARGET = server_scheduler
SRC = server_scheduler.c
OBJ = server_scheduler.o

all : $(TARGET)

$(TARGET) : $(OBJ)
	$(GCC) -o $@ $? $(LINK_MARIA_FLAG) 

%.o : %.c
	$(GCC) -o $@ -c $< $(C_FLAGS) 

clean :
	rm -rf $(OBJ)

fclean : clean
	rm -rf $(TARGET)

re : clean fclean all

.PHONY : all clean fclean re