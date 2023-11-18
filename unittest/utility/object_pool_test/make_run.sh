#!/bin/bash

target=unittest.out

rm $target
g++ -g unittest.cpp -I ../../../src -o $target -lpthread -std=c++11
./$target