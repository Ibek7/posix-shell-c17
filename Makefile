CC = cc
CFLAGS = -std=c17 -Wall -Wextra -pedantic -g
TARGET = myshell

all: $(TARGET)

$(TARGET): myshell.c
	$(CC) $(CFLAGS) -o $(TARGET) myshell.c

clean:
	rm -f $(TARGET)
