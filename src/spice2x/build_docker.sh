#!/bin/bash
export DOCKER_BUILDKIT=0

docker build --pull $PWD/external/docker -t spicetools/deps --platform linux/x86_64
docker build --build-context gitroot=$PWD/../../.git . -t spicetools/spice:latest --no-cache
docker run --rm -it -v $PWD/dist:/src/src/spice2x/dist -v $PWD/bin:/src/src/spice2x/bin spicetools/spice
