# ================================================================
#  TaskForge — Makefile
#  Build: make          Run: make run       Clean: make clean
# ================================================================

# Use system gcc; on MSYS2 set PATH to include /mingw64/bin
CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -Iinclude
LDFLAGS = -lpthread

SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj

# Source files
SRCS    = main.c \
          $(SRC_DIR)/process_mgmt.c \
          $(SRC_DIR)/scheduler.c \
          $(SRC_DIR)/deadlock.c \
          $(SRC_DIR)/memory_mgmt.c \
          $(SRC_DIR)/io_file_mgmt.c \
          $(SRC_DIR)/task_ops.c

# Object files (all go into obj/)
OBJS    = $(OBJ_DIR)/main.o \
          $(OBJ_DIR)/process_mgmt.o \
          $(OBJ_DIR)/scheduler.o \
          $(OBJ_DIR)/deadlock.o \
          $(OBJ_DIR)/memory_mgmt.o \
          $(OBJ_DIR)/io_file_mgmt.o \
          $(OBJ_DIR)/task_ops.o

TARGET  = taskforge

# ── Default target ──
all: $(OBJ_DIR) $(TARGET)

# Create obj directory
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Link
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo ""
	@echo "  Build successful! Run with: ./taskforge"
	@echo ""

# Compile main.c
$(OBJ_DIR)/main.o: main.c $(INC_DIR)/common.h $(INC_DIR)/process_mgmt.h $(INC_DIR)/scheduler.h $(INC_DIR)/deadlock.h $(INC_DIR)/memory_mgmt.h $(INC_DIR)/io_file_mgmt.h $(INC_DIR)/task_ops.h
	$(CC) $(CFLAGS) -c $< -o $@

# Compile source modules
$(OBJ_DIR)/process_mgmt.o: $(SRC_DIR)/process_mgmt.c $(INC_DIR)/process_mgmt.h $(INC_DIR)/common.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/scheduler.o: $(SRC_DIR)/scheduler.c $(INC_DIR)/scheduler.h $(INC_DIR)/common.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/deadlock.o: $(SRC_DIR)/deadlock.c $(INC_DIR)/deadlock.h $(INC_DIR)/common.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/memory_mgmt.o: $(SRC_DIR)/memory_mgmt.c $(INC_DIR)/memory_mgmt.h $(INC_DIR)/common.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/io_file_mgmt.o: $(SRC_DIR)/io_file_mgmt.c $(INC_DIR)/io_file_mgmt.h $(INC_DIR)/common.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/task_ops.o: $(SRC_DIR)/task_ops.c $(INC_DIR)/task_ops.h $(INC_DIR)/common.h
	$(CC) $(CFLAGS) -c $< -o $@

# ── Run ──
run: all
	./$(TARGET)

# ── Clean ──
clean:
	rm -rf $(OBJ_DIR) $(TARGET) $(TARGET).exe

.PHONY: all clean run
