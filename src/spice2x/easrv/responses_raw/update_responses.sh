#!/usr/bin/env bash

set -euxo pipefail

cd -P "$( dirname "$(readlink -f "$0")" )"
mkdir -p ../responses

for file in *.bin
do
	xxd -C -i "${file}" > "../responses/$(basename "${file}" .bin).h"
done
