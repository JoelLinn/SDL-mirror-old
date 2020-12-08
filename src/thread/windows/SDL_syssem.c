/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2020 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"

#if SDL_THREAD_WINDOWS

/**
 * Semaphore functions using the Win32 API
 * There are two implementations available based on:
 * - Kernel Semaphores. Available on all OS versions. (kern)
 *   Heavy-weight inter-process kernel objects.
 * - Slim Reader/Writer Locks and Condition Variables. (cond)
 *   Faster due to significantly less context switches.
 *   Requires Windows Vista or newer.
 * which are chosen at runtime.
*/

#include "../../core/windows/SDL_windows.h"

#include "SDL_hints.h"
#include "SDL_thread.h"

typedef SDL_sem * (*pfnSDL_CreateSemaphore)(Uint32);
typedef void (*pfnSDL_DestroySemaphore)(SDL_sem *);
typedef int (*pfnSDL_SemWaitTimeout)(SDL_sem *, Uint32);
typedef int (*pfnSDL_SemTryWait)(SDL_sem *);
typedef int (*pfnSDL_SemWait)(SDL_sem *);
typedef Uint32 (*pfnSDL_SemValue)(SDL_sem *);
typedef int (*pfnSDL_SemPost)(SDL_sem *);

typedef struct SDL_semaphore_impl_t
{
    pfnSDL_CreateSemaphore  Create;
    pfnSDL_DestroySemaphore Destroy;
    pfnSDL_SemWaitTimeout   WaitTimeout;
    pfnSDL_SemTryWait       TryWait;
    pfnSDL_SemWait          Wait;
    pfnSDL_SemValue         Value;
    pfnSDL_SemPost          Post;
} SDL_sem_impl_t;

/* Implementation will be chosen at runtime based on available Kernel features */
static SDL_sem_impl_t SDL_sem_impl_active = {0};


/**
 * SRW Lock + Condition Variable implementation
 */

#ifndef SRWLOCK_INIT
#define SRWLOCK_INIT {0}
typedef struct _SRWLOCK {
    PVOID Ptr;
} SRWLOCK, *PSRWLOCK;
#endif
#ifndef CONDITION_VARIABLE_INIT
#define CONDITION_VARIABLE_INIT {0}
typedef struct _CONDITION_VARIABLE {
    PVOID Ptr;
} CONDITION_VARIABLE, *PCONDITION_VARIABLE;
#endif

typedef VOID(WINAPI *pfnReleaseSRWLockExclusive)(PSRWLOCK);
typedef VOID(WINAPI *pfnAcquireSRWLockExclusive)(PSRWLOCK);
typedef VOID(WINAPI *pfnWakeConditionVariable)(PCONDITION_VARIABLE);
typedef BOOL(WINAPI *pfnSleepConditionVariableSRW)
  (PCONDITION_VARIABLE, PSRWLOCK, DWORD, ULONG);

static pfnReleaseSRWLockExclusive pReleaseSRWLockExclusive = NULL;
static pfnAcquireSRWLockExclusive pAcquireSRWLockExclusive = NULL;
static pfnWakeConditionVariable pWakeConditionVariable = NULL;
static pfnSleepConditionVariableSRW pSleepConditionVariableSRW = NULL;

typedef struct SDL_semaphore_cond
{
    SRWLOCK lock;
    CONDITION_VARIABLE cond;
    Uint32 count;
} SDL_sem_cond;

static SDL_sem *
SDL_CreateSemaphore_cond(Uint32 initial_value)
{
    SDL_sem_cond *sem;

    /* Relies on SRWLOCK_INIT == CONDITION_VARIABLE_INIT == 0 */
    sem = (SDL_sem_cond *) SDL_calloc(1, sizeof(*sem));
    if (sem) {
        sem->count = initial_value;
    } else {
        SDL_OutOfMemory();
    }
    return (SDL_sem *)sem;
}

static void
SDL_DestroySemaphore_cond(SDL_sem * sem)
{
    if (sem) {
        /* There are no kernel allocated ressources */
        SDL_free(sem);
    }
}

static int
SDL_SemWaitTimeout_cond(SDL_sem * _sem, Uint32 timeout)
{
    SDL_sem_cond *sem = (SDL_sem_cond *)_sem;
    Uint32 now;
    Uint32 deadline;
    DWORD timeout_eff;

    if (timeout == SDL_MUTEX_MAXWAIT) {
        return SDL_SemWait(_sem);
    }

    if (!sem) {
        return SDL_SetError("Passed a NULL sem");
    }

    /**
     * The Condition Variable is subject to spurious and stolen
     * wakeups so we need to recalculate the effective timeout.
     */
    now = SDL_GetTicks();
    timeout_eff = (DWORD) timeout;
    deadline = now + timeout_eff;

    pAcquireSRWLockExclusive(&sem->lock);
    while (sem->count == 0) {
        if (pSleepConditionVariableSRW(&sem->cond, &sem->lock, timeout_eff, 0) == FALSE) {
            /* Handle the error and return */
            pReleaseSRWLockExclusive(&sem->lock);
            if (GetLastError() == ERROR_TIMEOUT) {
                return SDL_MUTEX_TIMEDOUT;
            }
            return SDL_SetError("SleepConditionVariableSRW() failed");
        }
        now = SDL_GetTicks();
        if (deadline > now) {
            timeout_eff = deadline - now;
        } else {
            pReleaseSRWLockExclusive(&sem->lock);
            return SDL_MUTEX_TIMEDOUT;
        }
    }
    /* Success, we did it! */
    --sem->count;
    pReleaseSRWLockExclusive(&sem->lock);

    return 0;
}

static int
SDL_SemTryWait_cond(SDL_sem * _sem)
{
    SDL_sem_cond *sem = (SDL_sem_cond *)_sem;
    int retval;

    if (!sem) {
        return SDL_SetError("Passed a NULL sem");
    }

    pAcquireSRWLockExclusive(&sem->lock);
    if (sem->count > 0) {
        --sem->count;
        retval = 0;
    } else {
        retval = SDL_MUTEX_TIMEDOUT;
    }
    pReleaseSRWLockExclusive(&sem->lock);

    return retval;
}

static int
SDL_SemWait_cond(SDL_sem * _sem)
{
    SDL_sem_cond *sem = (SDL_sem_cond *)_sem;

    if (!sem) {
        return SDL_SetError("Passed a NULL sem");
    }

    pAcquireSRWLockExclusive(&sem->lock);
    while (sem->count == 0) {
        if (pSleepConditionVariableSRW(&sem->cond, &sem->lock, INFINITE, 0) == FALSE) {
            pReleaseSRWLockExclusive(&sem->lock);
            return SDL_SetError("SleepConditionVariableSRW() failed");
        }
    }
    --sem->count;
    pReleaseSRWLockExclusive(&sem->lock);

    return 0;
}

static Uint32
SDL_SemValue_cond(SDL_sem * _sem)
{
    SDL_sem_cond *sem = (SDL_sem_cond *)_sem;
    Uint32 count;

    if (!sem) {
        SDL_SetError("Passed a NULL sem");
        return 0;
    }

    /* Could also lock in shared mode, but the lock overhead would */
    /* be much larger than the single copy operation we execute. */
    pAcquireSRWLockExclusive(&sem->lock);
    count = sem->count;
    pReleaseSRWLockExclusive(&sem->lock);

    return count;
}

static int
SDL_SemPost_cond(SDL_sem * _sem)
{
    SDL_sem_cond *sem = (SDL_sem_cond *)_sem;

    if (!sem) {
        return SDL_SetError("Passed a NULL sem");
    }

    pAcquireSRWLockExclusive(&sem->lock);
    ++sem->count;
    pReleaseSRWLockExclusive(&sem->lock);

    pWakeConditionVariable(&sem->cond);

    return 0;
}

static const SDL_sem_impl_t SDL_sem_impl_cond =
{
    &SDL_CreateSemaphore_cond,
    &SDL_DestroySemaphore_cond,
    &SDL_SemWaitTimeout_cond,
    &SDL_SemTryWait_cond,
    &SDL_SemWait_cond,
    &SDL_SemValue_cond,
    &SDL_SemPost_cond,
};


/**
 * Fallback Semaphore implementation using Kernel Semaphores
 */

typedef struct SDL_semaphore_kern
{
    HANDLE id;
    LONG count;
} SDL_sem_kern;

/* Create a semaphore */
static SDL_sem *
SDL_CreateSemaphore_kern(Uint32 initial_value)
{
    SDL_sem_kern *sem;

    /* Allocate sem memory */
    sem = (SDL_sem_kern *) SDL_malloc(sizeof(*sem));
    if (sem) {
        /* Create the semaphore, with max value 32K */
#if __WINRT__
        sem->id = CreateSemaphoreEx(NULL, initial_value, 32 * 1024, NULL, 0, SEMAPHORE_ALL_ACCESS);
#else
        sem->id = CreateSemaphore(NULL, initial_value, 32 * 1024, NULL);
#endif
        sem->count = initial_value;
        if (!sem->id) {
            SDL_SetError("Couldn't create semaphore");
            SDL_free(sem);
            sem = NULL;
        }
    } else {
        SDL_OutOfMemory();
    }
    return (SDL_sem *)sem;
}

/* Free the semaphore */
static void
SDL_DestroySemaphore_kern(SDL_sem * _sem)
{
    SDL_sem_kern *sem = (SDL_sem_kern *)_sem;
    if (sem) {
        if (sem->id) {
            CloseHandle(sem->id);
            sem->id = 0;
        }
        SDL_free(sem);
    }
}

static int
SDL_SemWaitTimeout_kern(SDL_sem * _sem, Uint32 timeout)
{
    SDL_sem_kern *sem = (SDL_sem_kern *)_sem;
    int retval;
    DWORD dwMilliseconds;

    if (!sem) {
        return SDL_SetError("Passed a NULL sem");
    }

    if (timeout == SDL_MUTEX_MAXWAIT) {
        dwMilliseconds = INFINITE;
    } else {
        dwMilliseconds = (DWORD) timeout;
    }
    switch (WaitForSingleObjectEx(sem->id, dwMilliseconds, FALSE)) {
    case WAIT_OBJECT_0:
        InterlockedDecrement(&sem->count);
        retval = 0;
        break;
    case WAIT_TIMEOUT:
        retval = SDL_MUTEX_TIMEDOUT;
        break;
    default:
        retval = SDL_SetError("WaitForSingleObject() failed");
        break;
    }
    return retval;
}

static int
SDL_SemTryWait_kern(SDL_sem * sem)
{
    return SDL_SemWaitTimeout_kern(sem, 0);
}

static int
SDL_SemWait_kern(SDL_sem * sem)
{
    return SDL_SemWaitTimeout_kern(sem, SDL_MUTEX_MAXWAIT);
}

/* Returns the current count of the semaphore */
static Uint32
SDL_SemValue_kern(SDL_sem * _sem)
{
    SDL_sem_kern *sem = (SDL_sem_kern *)_sem;
    if (!sem) {
        SDL_SetError("Passed a NULL sem");
        return 0;
    }
    return (Uint32)sem->count;
}

static int
SDL_SemPost_kern(SDL_sem * _sem)
{
    SDL_sem_kern *sem = (SDL_sem_kern *)_sem;
    if (!sem) {
        return SDL_SetError("Passed a NULL sem");
    }
    /* Increase the counter in the first place, because
     * after a successful release the semaphore may
     * immediately get destroyed by another thread which
     * is waiting for this semaphore.
     */
    InterlockedIncrement(&sem->count);
    if (ReleaseSemaphore(sem->id, 1, NULL) == FALSE) {
        InterlockedDecrement(&sem->count);      /* restore */
        return SDL_SetError("ReleaseSemaphore() failed");
    }
    return 0;
}

static const SDL_sem_impl_t SDL_sem_impl_kern =
{
    &SDL_CreateSemaphore_kern,
    &SDL_DestroySemaphore_kern,
    &SDL_SemWaitTimeout_kern,
    &SDL_SemTryWait_kern,
    &SDL_SemWait_kern,
    &SDL_SemValue_kern,
    &SDL_SemPost_kern,
};


/**
 * Runtime selection and redirection
 */

SDL_sem *
SDL_CreateSemaphore(Uint32 initial_value)
{
    if (SDL_sem_impl_active.Create == NULL) {
        /* Default to fallback implementation */
        const SDL_sem_impl_t * impl = &SDL_sem_impl_kern;

        if (!SDL_GetHintBoolean(SDL_HINT_WINDOWS_FORCE_SEMAPHORE_KERNEL, SDL_FALSE)) {
            HMODULE kernel32 = LoadLibraryW(L"kernel32.dll");
            if (kernel32) {
                /* Try to load required functions provided by Vista or newer */
                pReleaseSRWLockExclusive = (pfnReleaseSRWLockExclusive) GetProcAddress(kernel32, "ReleaseSRWLockExclusive");
                pAcquireSRWLockExclusive = (pfnAcquireSRWLockExclusive) GetProcAddress(kernel32, "AcquireSRWLockExclusive");
                pWakeConditionVariable = (pfnWakeConditionVariable) GetProcAddress(kernel32, "WakeConditionVariable");
                pSleepConditionVariableSRW  = (pfnSleepConditionVariableSRW) GetProcAddress(kernel32, "SleepConditionVariableSRW");
                if(pReleaseSRWLockExclusive && pAcquireSRWLockExclusive &&
                   pWakeConditionVariable && pSleepConditionVariableSRW) {
                    impl = &SDL_sem_impl_cond;
                }
            }
        }

        /* Copy instead of using pointer to save one level of indirection */
        SDL_memcpy(&SDL_sem_impl_active, impl, sizeof(SDL_sem_impl_active));
    }
    return SDL_sem_impl_active.Create(initial_value);
}

void
SDL_DestroySemaphore(SDL_sem * sem)
{
    SDL_sem_impl_active.Destroy(sem);
}

int
SDL_SemWaitTimeout(SDL_sem * sem, Uint32 timeout)
{
    return SDL_sem_impl_active.WaitTimeout(sem, timeout);
}

int
SDL_SemTryWait(SDL_sem * sem)
{
    return SDL_sem_impl_active.TryWait(sem);
}

int
SDL_SemWait(SDL_sem * sem)
{
    return SDL_sem_impl_active.Wait(sem);
}

Uint32
SDL_SemValue(SDL_sem * sem)
{
    return SDL_sem_impl_active.Value(sem);
}

int
SDL_SemPost(SDL_sem * sem)
{
    return SDL_sem_impl_active.Post(sem);
}

#endif /* SDL_THREAD_WINDOWS */

/* vi: set ts=4 sw=4 expandtab: */
