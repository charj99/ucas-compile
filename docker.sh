#!/usr/bin/env bash
# docker.sh

action=$1

if [ $action = "enter" ]; then
    sudo docker exec -it compile-course /bin/bash
elif [ $action = "build" ]; then
    sudo docker-compose build
elif [ $action = "up" ]; then
    sudo docker-compose up -d
    echo "127.0.0.1:2222"
elif [ $action = "down" ]; then
    sudo docker-compose down
elif [ $action = "debug" ]; then
    sudo docker-compose exec llvm sh -c "gdbserver :1234 /home/deploy/assignment1/src/cmake-build-debug/ast-interpreter ${*:2}"
fi
