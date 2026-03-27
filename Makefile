CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c11 -pthread -D_DEFAULT_SOURCE
CFLAGS += $(shell pkg-config --cflags jack sndfile alsa sdl2)
LDFLAGS = $(shell pkg-config --libs jack sndfile alsa sdl2) -lm -lpthread

# Source files
SRC     = src/main.c src/config.c src/sampler.c src/voice.c src/mixer.c \
          src/midi.c src/keyboard.c src/console.c src/web.c \
          src/jack_engine.c src/ring_buffer.c
VENDOR  = vendor/tomlc99/toml.c vendor/mongoose/mongoose.c
OBJ     = $(SRC:.c=.o) $(VENDOR:.c=.o)

# Include paths
CFLAGS += -Ivendor/tomlc99 -Ivendor/mongoose -Isrc

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

help:
	@echo "VirtualOrgan — Build Targets"
	@echo ""
	@echo "  make              Build the organ engine"
	@echo "  make clean        Remove build artifacts"
	@echo "  make gen-samples  Generate sine wave test samples in samples/test/"
	@echo "  make help         Show this help"
	@echo ""
	@echo "Usage:"
	@echo "  ./organ-engine <config.toml> [--fake-midi | --keyboard]"
	@echo ""
	@echo "Examples:"
	@echo "  ./organ-engine test/test_config.toml --fake-midi   Test with generated sine waves"
	@echo "  ./organ-engine test/burea_config.toml --fake-midi  Test with Bureå organ samples"
	@echo "  ./organ-engine test/burea_config.toml --keyboard   Play with computer keyboard"
	@echo "  ./organ-engine test/burea_config.toml              Real MIDI keyboard input"

.PHONY: all clean gen-samples help
