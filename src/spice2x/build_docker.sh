#!/bin/bash
export DOCKER_BUILDKIT=0

docker build --pull $PWD/external/docker -t spicetools/deps --platform linux/x86_64
docker build . -t spicetools/spice:latest --no-cache
docker run --rm -it -v $PWD/dist:/src/dist -v $PWD/bin:/src/bin spicetools/spice
