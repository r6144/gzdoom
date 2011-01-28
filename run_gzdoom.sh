#!/bin/bash

export DRI_DRIVER=r600g
xgamma -gamma 1.6
./gzdoom "$@"
xgamma -gamma 1.0
