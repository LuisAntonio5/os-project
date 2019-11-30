CC = gcc
  CFLAGS  = -lpthread -D_REENTRANT -Wall -pthread

  # the build target executable:
  TARGET = main

  all: $(TARGET)

  $(TARGET): $(TARGET).c header.h
			$(CC) $(CFLAGS) $(TARGET).c header.h -o $(TARGET)
