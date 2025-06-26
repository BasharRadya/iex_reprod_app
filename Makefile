# Makefile for Network Application

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pthread -O2
LDFLAGS = -pthread

# Target executable
TARGET = network_app

# Source files
SOURCES = main.c server.c client.c utils.c
OBJECTS = $(SOURCES:.c=.o)
HEADERS = network_app.h

# Default target
all: $(TARGET)

# Build the executable
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

# Compile source files
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up build artifacts
clean:
	rm -f $(OBJECTS) $(TARGET)

# Clean and rebuild
rebuild: clean all

# Install target (optional)
install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

# Uninstall target (optional)
uninstall:
	rm -f /usr/local/bin/$(TARGET)

# Help target
help:
	@echo "Available targets:"
	@echo "  all      - Build the application (default)"
	@echo "  clean    - Remove build artifacts"
	@echo "  rebuild  - Clean and rebuild"
	@echo "  install  - Install to /usr/local/bin"
	@echo "  uninstall- Remove from /usr/local/bin"
	@echo "  help     - Show this help message"

# Debug build
debug: CFLAGS += -g -DDEBUG
debug: $(TARGET)

# Release build with optimizations
release: CFLAGS += -O3 -DNDEBUG
release: $(TARGET)

# Show compiler version and flags
info:
	@echo "Compiler: $(CC)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"
	@echo "Sources: $(SOURCES)"
	@echo "Target: $(TARGET)"

.PHONY: all clean rebuild install uninstall help debug release info 