CC=gcc
CFLAG = -g
all:oss user_proc
target:oss
target:%.o
	$(CC) $(CFLAG) -c $< -o $@

oss:oss.o
	$(CC) $(CFLAG) $^ -o $@ -lpthread

user_proc:user_proc.o
	$(CC) $(CFLAGS) $^ -o $@ -lpthread

clean:
	rm -f *.o oss user_proc 
