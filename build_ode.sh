#!/bin/bash
# Helper script to build ODE library if needed

cd "$(dirname "$0")/external/ode"

if [ ! -f ode/src/.libs/libode.a ]; then
    echo "Building ODE library..."
    ./configure && make all
else
    echo "ODE library already built"
fi
