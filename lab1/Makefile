# Makefile for Monitor Lab

CXX = g++
CXXFLAGS = -std=c++11 -pthread -Wall -Wextra
TARGET = monitor
SOURCE = main.cpp

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) $(SOURCE) -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) $(TARGET).exe

.PHONY: all run clean
