#docker run --rm -v "$(PWD)":/usr/src/multi-process-web-server -w /usr/src/multi-process-web-server -it gcc:4.9 bash
docker run --rm -v "$(PWD)":/usr/src/multi-process-web-server -w /usr/src/multi-process-web-server -it rikorose/gcc-cmake bash
