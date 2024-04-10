server: main.cpp ./threadpool/threadpool.h ./http/http_conn.cpp ./http/http_connect.h ./lock/locker.h ./log/log.cpp ./log/log.h ./log/block_queue.h ./Cmysql/sql_connection_pool.cpp ./Cmysql/sql_connection_pool.h
	g++ -g -o server.out main.cpp ./threadpool/threadpool.h ./http/http_conn.cpp ./http/http_connect.h ./lock/locker.h ./log/log.cpp ./log/log.h ./Cmysql/sql_connection_pool.cpp ./Cmysql/sql_connection_pool.h -lpthread -lmysqlclient

