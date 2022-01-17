#!/bin/bash
cd `dirname $0`
N=$((`nproc` - 1))
make -j$N NPROC=$N
