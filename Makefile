# Makefile for WebServer

CXX = g++
CXXFLAGS = -std=c++14 -O2 -Wall -pthread
LDFLAGS = -lmysqlclient -lssl -lcrypto
TARGET = webserver
SOURCES = main.cpp

# 默认目标
all: $(TARGET)

# 编译目标
$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET) $(LDFLAGS)

# 清理
clean:
	rm -f $(TARGET)

# 运行
run: $(TARGET)
	./$(TARGET)

# 调试版本
debug: CXXFLAGS = -std=c++14 -g -Wall -pthread -DDEBUG
debug: $(TARGET)

.PHONY: all clean run debug

