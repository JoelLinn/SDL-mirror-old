/*
  Copyright (C) 2020 Joel Linn <jl@conductive.de>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely.
*/

#include "SDL.h"

/*
  Basic tests for mutex. It is very difficult to time tests so that they
  hit all edge cases deterministically.
  Hence don't just rely on them succeeding.
*/

#define NReentry 20
#define TResolutionMs 50

static
void RunBasicTest()
{
    SDL_mutex *mutex;

    SDL_Log("\ncreate/destroy ---------------------------------\n\n");

    mutex = SDL_CreateMutex();
    SDL_assert(mutex);
    SDL_Log("Created");
    SDL_DestroyMutex(mutex);
    SDL_Log("Destroyed");

    SDL_Log("\nlock/unlock ------------------------------------\n\n");
    mutex = SDL_CreateMutex();
    SDL_assert(mutex);
    SDL_Log("Created");
    SDL_assert(!SDL_LockMutex(mutex));
    SDL_Log("Locked");
    SDL_assert(!SDL_UnlockMutex(mutex));
    SDL_Log("Unlocked");
    SDL_DestroyMutex(mutex);
    SDL_Log("Destroyed");

    SDL_Log("\ntrylock/unlock ---------------------------------\n\n");
    mutex = SDL_CreateMutex();
    SDL_assert(mutex);
    SDL_Log("Created");
    SDL_assert(!SDL_TryLockMutex(mutex));
    SDL_Log("Try-Locked");
    SDL_assert(!SDL_UnlockMutex(mutex));
    SDL_Log("Unlocked");
    SDL_DestroyMutex(mutex);
    SDL_Log("Destroyed");

    SDL_Log("\n(try)lock/unlock reentry -----------------------\n\n");
    mutex = SDL_CreateMutex();
    for (int i = 0; i < NReentry; i++)
    {
        if (i % 2) {
            SDL_assert(!SDL_LockMutex(mutex));
            SDL_Log("Locked     %02d", i);
        }
        else {
            SDL_assert(!SDL_TryLockMutex(mutex));
            SDL_Log("Try-Locked %02d", i);
        }
    }
    for (int i = NReentry - 1; i >= 0 ; i--)
    {
        SDL_assert(!SDL_UnlockMutex(mutex));
        SDL_Log("Unlocked   %02d", i);
    }
    SDL_DestroyMutex(mutex);
}


/* tick count history for verification */
# define NHistLength 6
static Uint32 parallel_history_main[NHistLength];
static Uint32 parallel_history_cont[NHistLength];

static
int SDLCALL contender(void *_mutex)
{
    SDL_mutex *mutex = (SDL_mutex *)_mutex;
    size_t idx = 0;
    int status;

    parallel_history_cont[idx++] = SDL_GetTicks();
    
    status = SDL_TryLockMutex(mutex);
    SDL_assert(status == SDL_MUTEX_TIMEDOUT);

    // Spin on the lock. 
    do {
        status = SDL_TryLockMutex(mutex);
    } while (status == SDL_MUTEX_TIMEDOUT);
    SDL_assert(!status);
    parallel_history_cont[idx++] = SDL_GetTicks();

    SDL_Delay(TResolutionMs);
    parallel_history_cont[idx++] = SDL_GetTicks();
    // Main tries to lock but fails
    SDL_Delay(TResolutionMs);
    
    parallel_history_cont[idx++] = SDL_GetTicks();
    // Give the lock to main
    SDL_UnlockMutex(mutex);

    SDL_Delay(TResolutionMs);
    parallel_history_cont[idx++] = SDL_GetTicks();
    // Main now re-enters once and unlocks
    SDL_assert(!SDL_LockMutex(mutex));
    parallel_history_cont[idx++] = SDL_GetTicks();

    return 0;
}

static
void RunParallelTest()
{
    SDL_mutex *mutex;
    SDL_Thread *thread;
    int idx = 0;

    SDL_Log("\nparallel test ----------------------------------\n\n");

    mutex = SDL_CreateMutex();
    SDL_assert(mutex);
    SDL_assert(!SDL_LockMutex(mutex));
    parallel_history_main[idx++] = SDL_GetTicks();
    SDL_Delay(TResolutionMs);

    thread = SDL_CreateThread(contender, "Contender", mutex);
    SDL_assert(thread);

    // Contender tries to lock (spinning)
    SDL_Delay(TResolutionMs);
    parallel_history_main[idx++] = SDL_GetTicks();
    SDL_Delay(TResolutionMs);

    SDL_assert(!SDL_UnlockMutex(mutex));
    // Contender now owns the mutex
    parallel_history_main[idx++] = SDL_GetTicks();

    SDL_Delay(TResolutionMs);
    parallel_history_main[idx++] = SDL_GetTicks();
    // Still owns it
    SDL_assert(SDL_TryLockMutex(mutex) == SDL_MUTEX_TIMEDOUT);
    // So we wait
    SDL_assert(!SDL_LockMutex(mutex));
    parallel_history_main[idx++] = SDL_GetTicks();

    // Re-enter the Lock
    SDL_assert(!SDL_LockMutex(mutex));
    SDL_assert(!SDL_UnlockMutex(mutex));
    SDL_Delay(TResolutionMs);
    parallel_history_main[idx++] = SDL_GetTicks();

    // Contender is waiting
    SDL_Delay(TResolutionMs);
    SDL_assert(!SDL_UnlockMutex(mutex));

    SDL_WaitThread(thread, NULL);
    SDL_DestroyMutex(mutex);

    /* After the test, check if the timings are in order */
    for (idx = 0; idx < NHistLength; idx++)
    {        
        SDL_Log("Milestone %2u after %4u and %4u ms.", idx, parallel_history_main[idx], parallel_history_cont[idx]);
        SDL_assert(parallel_history_main[idx] < parallel_history_cont[idx]);
    }
}

int
main(int argc, char *argv[])
{
    /* Enable standard application logging */
    SDL_LogSetPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO);

    /* Single-threaded test */
    RunBasicTest();

    /* Run two threads in parallel and record tick counts for verification */
    RunParallelTest();

    return 0;
}

/* vi: set ts=4 sw=4 expandtab: */
