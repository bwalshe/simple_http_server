#!/bin/bash


for i in {1..10000}; do
    curl http://localhost:8080/hello &
done
