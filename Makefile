CXX := g++
CXXFLAGS := -std=c++17 -Wall -O2
LDFLAGS_SERVER := -lsqlite3 -lncurses -lpthread
LDFLAGS_SIM := -lpthread
TSAN_CXXFLAGS := -std=c++17 -Wall -O1 -g -fsanitize=thread
TSAN_LDFLAGS_SERVER := -lsqlite3 -lncurses -lpthread -fsanitize=thread

.PHONY: all server device_sim server-tsan clean run-server run-sim1 run-sim2 run-sim3 run-stress50

all: server device_sim

server: server/main.cpp server/*.h common/protocol.h
	$(CXX) $(CXXFLAGS) -o server/patient_server server/main.cpp $(LDFLAGS_SERVER)

server-tsan: server/main.cpp server/*.h common/protocol.h
	$(CXX) $(TSAN_CXXFLAGS) -o server/patient_server_tsan server/main.cpp $(TSAN_LDFLAGS_SERVER)

device_sim: device_sim/main.cpp common/protocol.h
	$(CXX) $(CXXFLAGS) -o device_sim/device_sim device_sim/main.cpp $(LDFLAGS_SIM)

clean:
	rm -f server/patient_server server/patient_server_tsan device_sim/device_sim
	rm -f server/patient_monitor.db server/patient_monitor.db-shm server/patient_monitor.db-wal

run-server: server
	cd server && ./patient_server

run-sim1: device_sim
	cd device_sim && ./device_sim 1

run-sim2: device_sim
	cd device_sim && ./device_sim 2

run-sim3: device_sim
	cd device_sim && ./device_sim 3

run-stress50: device_sim
	cd device_sim && ./device_sim --count 50 server 8080
