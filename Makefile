CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c11 -pthread -D_DEFAULT_SOURCE
CFLAGS += $(shell pkg-config --cflags jack sndfile alsa)
LDFLAGS = $(shell pkg-config --libs jack sndfile alsa) -lm -lpthread

# Source files
SRC     = src/main.c src/config.c src/sampler.c src/voice.c src/mixer.c \
          src/midi.c src/jack_engine.c src/ring_buffer.c
VENDOR  = vendor/tomlc99/toml.c
OBJ     = $(SRC:.c=.o) $(VENDOR:.c=.o)

# Include paths
CFLAGS += -Ivendor/tomlc99 -Isrc

TARGET  = organ-engine

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Generate test sample WAV files
gen-samples: test/gen_test_samples.c
	$(CC) $(CFLAGS) -o $@ $< $(shell pkg-config --libs sndfile) -lm
	mkdir -p samples/test
	./gen-samples samples/test

clean:
	rm -f $(OBJ) $(TARGET) gen-samples

.PHONY: all clean gen-samples
