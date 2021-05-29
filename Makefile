CC := gcc
OPT := -O2 -w
SDL2_FLAGS := -DSDL2 `sdl2-config --cflags --libs`
EMCC_FLAGS := -DSDL2 -DSDL2_DOUBLE_QUEUE -s USE_SDL=2

all: cpcec zxsec xrf

cpcec_em: cpcec.c *.h
	mkdir roms && cp -f cpc*.rom roms/
	emcc $< $(OPT) $(EMCC_FLAGS) \
	-o index.html \
	--pre-js pre.js \
	--preload-file ./roms@/ \
	--embed-file .cpcecrc

%ec: %ec.c *.h
	$(CC) $(SDL2_FLAGS) $(OPT) $< -o $@

xrf: xrf.c
	$(CC) $(OPT) $< -o $@

clean: 
	rm -f cpcec zxsec xrf index.*

.PHONY: clean all cpcec_em
