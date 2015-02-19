#Setting _BSD_SOURCE so we can use daemon()
override CFLAGS += -std=gnu99 -Wall -Wextra

all: teapotd.o
	$(CC) $(CFLAGS) teapotd.o -o teapotd

