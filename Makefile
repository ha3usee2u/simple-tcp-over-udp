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
	rm -f logs/* valgrind_logs/*
	rm -rf downloads/*

clean-logs:
	rm -f logs/* valgrind_logs/*
	rm -rf downloads/*

run-server:
	./server

run-client:
	./client

run-clients:
	@echo "🧹 清除 logs 中的舊檔案..."
	@mkdir -p logs
	@rm -f logs/*
	@echo "🚀 啟動 $(N) 個 client..."
	@for i in $$(seq 1 $(N)); do \
		./client > logs/client$$i.log & \
    done

valgrind-server:
	valgrind --leak-check=full ./server

valgrind-clients:
	@mkdir -p valgrind_logs
	@rm -f valgrind_logs/*
	@for i in $$(seq 1 $(N)); do \
		valgrind --leak-check=full --log-file=valgrind_logs/client$$i.log ./client& \
	done
	@echo "✅ 啟動 $(N) 個 client 並記錄 Valgrind log 至 valgrind_logs/"

clang-format:
	clang-format -i $(SRC_CLIENT) $(SRC_SERVER) $(HDR)