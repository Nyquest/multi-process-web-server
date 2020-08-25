#!/usr/bin/env bash
clear
docker run --rm -v "$(PWD)":/usr/src/multi-process-web-server -w /usr/src/multi-process-web-server gcc:4.9 g++ -std=c++11 webserver.cpp -o webserver &&
	echo "Builded" &&
	docker run --rm -v "$(PWD)":/usr/src/multi-process-web-server -w /usr/src/multi-process-web-server gcc:4.9 ./webserver
	  
