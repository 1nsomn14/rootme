# Makefile
all:
    gcc -D_FILE_OFFSET_BITS=64 fusefs.c `pkg-config fuse --cflags --libs` -o fusefs
    gcc -static -o poc poc.c utils.c -lm

clean:
    rm poc
    rm fusefs