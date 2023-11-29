# Variables
CC = /usr/bin/g++
CFLAGS = -fdiagnostics-color=always -pedantic -Wall -Wextra -O3 -std=c++17
INCLUDES = -I. -I./parser
SOURCES = $(wildcard *.cpp) $(wildcard rdf_parser/*.cpp) $(wildcard file_reader/*.cpp)
TARGET = FlexRML

# Rules
all: $(TARGET)

$(TARGET):
	$(CC) $(CFLAGS) $(INCLUDES) $(SOURCES) -o $(TARGET)

clean:
	rm -f $(TARGET)
