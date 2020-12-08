#!/bin/bash

i=0
while [ $i -lt 1000 ]
do

    SDL_WINDOWS_FORCE_SEMAPHORE_KERNEL=$(($i % 2)) ./testsem 
    ((i++))
done