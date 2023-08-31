CXX ?= g++

DEBUG ?= 1
ifeq ($(DEBUG), 1)
	CXXFLAGS += -g
else 
	CXXFLAGS += -O2
endif

server: main.cpp webserver.cpp ./timer/lst_timer.cpp ./http/http_conn.cpp
	$(CXX) -o server $^ $(CXXFLAGS) -lpthread

clean:
	rm -r server