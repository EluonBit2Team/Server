AR = ar
AR_FLAGS = rcs
GCC = gcc
TARGET = cJSON.a

HEADER = cJSON.h
SRC = $(HEADER:.h=.c)
OBJ = $(HEADER:.h=.o)

all : $(TARGET)

$(TARGET) : $(OBJ)
	$(AR) $(AR_FLAGS) $@ $?

%.o : %.c
	$(GCC) -o $@ -c $<

clean :
	rm -rf $(OBJ)
fclean : clean
	rm -rf $(TARGET)
re : clean fclean all

.PHONY : all clean fclean re