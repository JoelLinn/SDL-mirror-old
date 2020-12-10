#!/bin/bash

i=0
while [ $i -lt 100 ]
do
    SDL_WINDOWS_FORCE_MUTEX_CRITICAL_SECTIONS=0 ./testatomic_no_reentry
    SDL_WINDOWS_FORCE_MUTEX_CRITICAL_SECTIONS=0 ./testatomic
    SDL_WINDOWS_FORCE_MUTEX_CRITICAL_SECTIONS=1 ./testatomic
    ((i++))
done
