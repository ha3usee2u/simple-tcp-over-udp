# 編譯器與選項
CXX = g++
CXXFLAGS = -std=c++20 -Wall -O2

# 原始檔與標頭檔
SRC_CLIENT = client.cpp
SRC_SERVER = server.cpp protocol.cpp

HDR = packet.hpp connection.hpp protocol.hpp

# 目標檔案
OBJ_CLIENT = $(SRC_CLIENT:.cpp=.o)
OBJ_SERVER = $(SRC_SERVER:.cpp=.o)

TARGET_CLIENT = client
TARGET_SERVER = server

# 預設目標：編譯全部
all: $(TARGET_CLIENT) $(TARGET_SERVER)

# 編譯 client
$(TARGET_CLIENT): client.o
	$(CXX) $(CXXFLAGS) -o $@ $^

# 編譯 server（包含 protocol.o）
$(TARGET_SERVER): server.o protocol.o
	$(CXX) $(CXXFLAGS) -o $@ $^

# 編譯每個 .cpp
%.o: %.cpp $(HDR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 清除所有編譯產物
clean:
	rm -f *.o $(TARGET_CLIENT) $(TARGET_SERVER)
