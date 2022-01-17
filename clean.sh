#!/bin/bash
# There is no `make clean` target because it does not work: make tries to make .d files first.

cd `dirname $0`
if [[ `ls target/* 2>/dev/null` != "" ]]; then
	echo "rm -rf target/*"
	rm -rf target/*
fi
