#!/usr/bin/env bash
kill -9 $(ps -ef | grep webserver | awk '{print $2}')
