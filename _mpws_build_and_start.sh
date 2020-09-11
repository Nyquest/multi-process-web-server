#!/usr/bin/env bash
cmake .
make
if [ $? -ne 0 ]; then
	echo BUILD - FAILED
	exit 1
fi

echo
echo BUILD - OK
echo

./webserver -h 0.0.0.0 -p 11777 -d "/usr/src/multi-process-web-server/static-site"
if [ $? -ne 0 ]; then
        echo START - FAILED
        exit 1
fi

echo
echo START - OK
echo

tail -fn1111 webserver.log
