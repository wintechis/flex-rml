# Variables
CC = g++ 
CFLAGS = -fdiagnostics-color=always -pedantic -Wall -Wextra -O3 -std=c++17
INCLUDES = -I. -I./parser
SOURCES = $(wildcard *.cpp) $(wildcard rdf_parser/*.cpp) $(wildcard file_reader/*.cpp)
OBJS = $(SOURCES:.cpp=.o)
TARGET = flexrml

# Declare phony targets
.PHONY: all clean  

# Default target
all: $(TARGET)

# Link object files to create the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(INCLUDES) $(OBJS) -o $(TARGET)

# Compile source files into object files
%.o: %.cpp
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Clean up generated files
clean:
	rm -f $(TARGET) $(OBJS)