CC := gcc
OPT := -O2 -w
SDL2_FLAGS := -DSDL2 `sdl2-config --cflags --libs`

all: cpcec zxsec xrf

%ec: %ec.c *.h
	$(CC) $(SDL2_FLAGS) $(OPT) $< -o $@

xrf: xrf.c
	$(CC) $(OPT) $< -o $@

clean: 
	rm -f cpcec zxsec xrf

.PHONY: clean all
