# Leonida Lights — client + multiplayer server
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
INCLUDES  = -Isrc $(shell pkg-config --cflags glfw3 glew freetype2 2>/dev/null)
LIBS_GL   = $(shell pkg-config --libs glfw3 glew freetype2 2>/dev/null) -lGL -ldl -lpthread
LIBS_NET  = -ldl -lpthread

CLIENT = gta6_clone
SERVER = gta6_server

.PHONY: all clean run run-server

all: $(CLIENT) $(SERVER)

$(CLIENT): src/main.cpp src/*.hpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ src/main.cpp $(LIBS_GL)

$(SERVER): src/server.cpp src/protocol.hpp src/net.hpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ src/server.cpp $(LIBS_NET)

run: $(CLIENT)
	./$(CLIENT)

run-server: $(SERVER)
	./$(SERVER)

clean:
	rm -f $(CLIENT) $(SERVER)
