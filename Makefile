# Makefile for Sp3ctra

# Compiler and flags
CXX = g++
CC = gcc
CXXFLAGS = -std=c++17 -O3 -ffast-math -Wall -Wextra -fPIC -DUSE_RTAUDIO -DPRINT_FPS -Wno-unused-but-set-variable -Wno-deprecated-declarations
CFLAGS = -O3 -ffast-math -Wall -Wextra -fPIC -DUSE_RTAUDIO -DPRINT_FPS -Wno-deprecated-declarations
LDFLAGS =
LIBS =

# Source files
SRCDIR = src/core
OBJDIR = build/obj

SOURCES_CPP = $(wildcard $(SRCDIR)/*.cpp)
SOURCES_C = $(filter-out $(SRCDIR)/audio.c, $(wildcard $(SRCDIR)/*.c)) $(wildcard $(SRCDIR)/kissfft/*.c)

OBJECTS_CPP = $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SOURCES_CPP))
OBJECTS_C = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SOURCES_C))

TARGET = Sp3ctra

# Platform specific settings
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS
    LIBS += -framework CoreFoundation -framework CoreAudio -framework AudioToolbox -framework Cocoa
    LIBS += -L/opt/homebrew/lib -lfftw3 -lsndfile -lrtaudio -lrtmidi -lsfml-graphics -lsfml-window -lsfml-system -lcsfml-graphics -lcsfml-window -lcsfml-system
    CXXFLAGS += -I/opt/homebrew/include
    CFLAGS += -I/opt/homebrew/include
    TARGET_PATH = build/$(TARGET)
else
    # Linux
    LIBS += -lfftw3 -lsndfile -lasound -lrtaudio -lrtmidi -lpthread
    TARGET_PATH = build/$(TARGET)
endif

.PHONY: all clean

all: $(TARGET_PATH)

$(TARGET_PATH): $(OBJECTS_CPP) $(OBJECTS_C)
	@mkdir -p $(@D)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJDIR) $(TARGET_PATH)
