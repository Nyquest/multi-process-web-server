g++ epoll_server.cpp -o epoll_server
if [ $? -eq 0 ]; then
	echo BUILD - OK
	./epoll_server
else
	echo BUILD - FAILED
fi
