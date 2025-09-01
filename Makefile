CC = cc
CFLAGS = -std=c17 -Wall -Wextra -pedantic -g
TARGET = myshell

all: $(TARGET)

$(TARGET): myshell.c
	$(CC) $(CFLAGS) -o $(TARGET) myshell.c

clean:
	rm -f $(TARGET)

test: $(TARGET)
	@echo "Running smoke tests..."
	@./$(TARGET) <<'EOF' > /tmp/myshell_test_out.txt 2>&1
echo hello | tr a-z A-Z
sleep 0 &
jobs
exit
EOF
	@echo "Smoke test output:"
	@cat /tmp/myshell_test_out.txt
