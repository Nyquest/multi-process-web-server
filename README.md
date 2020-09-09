# multi-process-web-server

## Запуск однопоточного epoll-сервера
`./_epoll_build_and_start.sh`

## Запуск многопроцессного веб-сервера (Multi-process web server)

*Сборка*

`./_build.sh`

или

`cmake .`

`make`

*Запуск*

`./_start.sh`

или

`./final -h <ip> -p <port> -d <directory>`

## Примеры запросов для однопоточного epoll-сервера

1) GET http://localhost:12345/
2) GET http://localhost:12345/index.html
3) GET http://localhost:12345/index.html?random=124
4) GET http://localhost:12345/js/scritps.js
5) GET http://localhost:12345/img/logo.png
6) POST http://localhost:12345/calc

`{
    "formula": "123+456"
}`