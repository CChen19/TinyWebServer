CXX ?= g++

DEBUG ?= 1
ifeq ($(DEBUG), 1)
    CXXFLAGS += -g
else
    CXXFLAGS += -O2

endif

SOURCES := main.cpp \
	webserver.cpp \
	config/config.cpp \
	log/log.cpp \
	CGImysql/sql_connection_pool.cpp \
	timer/lst_timer.cpp \
	http/http_conn.cpp \
	http/router.cpp \
	http/response.cpp \
	handler/health_handler.cpp \
	handler/short_url_handler.cpp \
	shorturl/base62.cpp \
	shorturl/bloom_filter.cpp \
	shorturl/singleflight.cpp \
	shorturl/snowflake.cpp \
	shorturl/short_url_cache.cpp \
	shorturl/short_url_repository.cpp

server: $(SOURCES)
	$(CXX) -std=c++14 -I. -Ithird_party -o server $^ $(CXXFLAGS) -lpthread -lmysqlclient -lyaml-cpp

clean:
	rm  -r server
