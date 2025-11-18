CC      := gcc
CFLAGS  := -std=c99 -O2 -Wall -Wextra -Iincludes $(shell pkg-config --cflags sdl2)
LDFLAGS := $(shell pkg-config --libs sdl2) -lm -lGL
TARGET  := emulator

SRCS    := main.c logging.c $(wildcard core/*.c)
OBJDIR  := build
OBJS    := $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))
DEPS    := $(OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

clean:
	rm -rf $(OBJDIR) $(TARGET)

.PHONY: all clean

