CXX ?= g++

DEBUG ?= 1
ifeq ($(DEBUG), 1)
	CXXFLAGS += -g
else 
	CXXFLAGS += -O2
endif

server: main.cpp webserver.cpp ./timer/lst_timer.cpp ./http/http_conn.cpp ./log/log.cpp ./CGImysql/sql_conn_pool.cpp
	$(CXX) -o server $^ $(CXXFLAGS) -lpthread -L/usr/lib64/mysql -lmysqlclient

clean:
	rm -r server