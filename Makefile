CC = gcc
CFLAGS = `pkg-config --cflags gtk+-3.0` -Wall -O2
LIBS = `pkg-config --libs gtk+-3.0`

SRC = src/main.c src/explorer.c src/ui.c src/utils.c
OUT = wo-files

all:
	$(CC) $(SRC) -o $(OUT) $(CFLAGS) $(LIBS)

clean:
	rm -f $(OUT)

run:
	./$(OUT)
