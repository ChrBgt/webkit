/*
 * Copyright (C) 2007, 2008 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer. 
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution. 
 * 3.  Neither the name of Apple Computer, Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * =============================================================================
 * Note:  The implementation of condition variables under the Windows
 * plaform was based on that of the excellent BOOST C++ library.  It
 * has been rewritten to fit in with the WebKit architecture and to
 * use its coding conventions.
 * =============================================================================
 *
 * The Boost license is virtually identical to the Apple variation at the
 * top of this file, but is included here for completeness:
 *
 * Boost Software License - Version 1.0 - August 17th, 2003
 *
 * Permission is hereby granted, free of charge, to any person or organization
 * obtaining a copy of the software and accompanying documentation covered by
 * this license (the "Software") to use, reproduce, display, distribute,
 * execute, and transmit the Software, and to prepare derivative works of the
 * Software, and to permit third-parties to whom the Software is furnished to
 * do so, all subject to the following:
 *
 * The copyright notices in the Software and this entire statement, including
 * the above license grant, this restriction and the following disclaimer,
 * must be included in all copies of the Software, in whole or in part, and
 * all derivative works of the Software, unless such copies or derivative
 * works are solely in the form of machine-executable object code generated by
 * a source language processor.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
 * SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
 * FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "config.h"
#include "Threading.h"

#include "MainThread.h"
#include <process.h>
#include <windows.h>
#include <wtf/HashMap.h>
#include <wtf/MathExtras.h>

#if PLATFORM(WIN)
// Currently, Apple's Windows port uses a mixture of native and pthreads functions in FastMalloc.
// To ensure that thread-specific data is properly destroyed, we need to end each thread with pthread_exit().
#include <pthread.h>
#endif

namespace WTF {

Mutex* atomicallyInitializedStaticMutex;

static ThreadIdentifier mainThreadIdentifier;

static Mutex& threadMapMutex()
{
    static Mutex mutex;
    return mutex;
}

void initializeThreading()
{
    if (!atomicallyInitializedStaticMutex) {
        atomicallyInitializedStaticMutex = new Mutex;
        threadMapMutex();
        wtf_random_init();
        initializeMainThread();
        mainThreadIdentifier = currentThread();
    }
}

static HashMap<DWORD, HANDLE>& threadMap()
{
    static HashMap<DWORD, HANDLE> map;
    return map;
}

static void storeThreadHandleByIdentifier(DWORD threadID, HANDLE threadHandle)
{
    MutexLocker locker(threadMapMutex());
    threadMap().add(threadID, threadHandle);
}

static HANDLE threadHandleForIdentifier(ThreadIdentifier id)
{
    MutexLocker locker(threadMapMutex());
    return threadMap().get(id);
}

static void clearThreadHandleForIdentifier(ThreadIdentifier id)
{
    MutexLocker locker(threadMapMutex());
    ASSERT(threadMap().contains(id));
    threadMap().remove(id);
}

struct ThreadFunctionInvocation {
    ThreadFunctionInvocation(ThreadFunction function, void* data) : function(function), data(data) {}

    ThreadFunction function;
    void* data;
};

static unsigned __stdcall wtfThreadEntryPoint(void* param)
{
    ThreadFunctionInvocation invocation = *static_cast<ThreadFunctionInvocation*>(param);
    delete static_cast<ThreadFunctionInvocation*>(param);

    void* result = invocation.function(invocation.data);

#if PLATFORM(WIN)
    // pthreads-win32 knows how to work with threads created with Win32 or CRT functions, so it's OK to mix APIs.
    pthread_exit(result);
#endif

    return reinterpret_cast<unsigned>(result);
}

ThreadIdentifier createThread(ThreadFunction entryPoint, void* data)
{
    unsigned threadIdentifier = 0;
    ThreadIdentifier threadID = 0;
    ThreadFunctionInvocation* invocation = new ThreadFunctionInvocation(entryPoint, data);
    HANDLE threadHandle = reinterpret_cast<HANDLE>(_beginthreadex(0, 0, wtfThreadEntryPoint, invocation, 0, &threadIdentifier));
    if (!threadHandle) {
        LOG_ERROR("Failed to create thread at entry point %p with data %p: %ld", entryPoint, data, errno);
        return 0;
    }

    threadID = static_cast<ThreadIdentifier>(threadIdentifier);
    storeThreadHandleByIdentifier(threadIdentifier, threadHandle);

    return threadID;
}

int waitForThreadCompletion(ThreadIdentifier threadID, void** result)
{
    ASSERT(threadID);
    
    HANDLE threadHandle = threadHandleForIdentifier(threadID);
    if (!threadHandle)
        LOG_ERROR("ThreadIdentifier %u did not correspond to an active thread when trying to quit", threadID);
 
    DWORD joinResult = ::WaitForSingleObject(threadHandle, INFINITE);
    if (joinResult == WAIT_FAILED)
        LOG_ERROR("ThreadIdentifier %u was found to be deadlocked trying to quit", threadID);

    ::CloseHandle(threadHandle);
    clearThreadHandleForIdentifier(threadID);

    return joinResult;
}

void detachThread(ThreadIdentifier threadID)
{
    ASSERT(threadID);
    
    HANDLE threadHandle = threadHandleForIdentifier(threadID);
    if (threadHandle)
        ::CloseHandle(threadHandle);
    clearThreadHandleForIdentifier(threadID);
}

ThreadIdentifier currentThread()
{
    return static_cast<ThreadIdentifier>(::GetCurrentThreadId());
}

bool isMainThread()
{
    return currentThread() == mainThreadIdentifier;
}

Mutex::Mutex()
{
    m_mutex.m_recursionCount = 0;
    ::InitializeCriticalSection(&m_mutex.m_internalMutex);
}

Mutex::~Mutex()
{
    ::DeleteCriticalSection(&m_mutex.m_internalMutex);
}

void Mutex::lock()
{
    ::EnterCriticalSection(&m_mutex.m_internalMutex);
    ++m_mutex.m_recursionCount;
}
    
bool Mutex::tryLock()
{
    // This method is modeled after the behavior of pthread_mutex_trylock,
    // which will return an error if the lock is already owned by the
    // current thread.  Since the primitive Win32 'TryEnterCriticalSection'
    // treats this as a successful case, it changes the behavior of several
    // tests in WebKit that check to see if the current thread already
    // owned this mutex (see e.g., IconDatabase::getOrCreateIconRecord)
    DWORD result = ::TryEnterCriticalSection(&m_mutex.m_internalMutex);
    
    if (result != 0) {       // We got the lock
        // If this thread already had the lock, we must unlock and
        // return false so that we mimic the behavior of POSIX's
        // pthread_mutex_trylock:
        if (m_mutex.m_recursionCount > 0) {
            ::LeaveCriticalSection(&m_mutex.m_internalMutex);
            return false;
        }

        ++m_mutex.m_recursionCount;
        return true;
    }

    return false;
}

void Mutex::unlock()
{
    --m_mutex.m_recursionCount;
    ::LeaveCriticalSection(&m_mutex.m_internalMutex);
}

static const long MaxSemaphoreCount = static_cast<long>(~0UL >> 1);

ThreadCondition::ThreadCondition()
{
    m_condition.m_timedOut = 0;
    m_condition.m_blocked = 0;
    m_condition.m_waitingForRemoval = 0;
    m_condition.m_gate = ::CreateSemaphore(0, 1, 1, 0);
    m_condition.m_queue = ::CreateSemaphore(0, 0, MaxSemaphoreCount, 0);
    m_condition.m_mutex = ::CreateMutex(0, 0, 0);

    if (!m_condition.m_gate || !m_condition.m_queue || !m_condition.m_mutex) {
        if (m_condition.m_gate)
            ::CloseHandle(m_condition.m_gate);
        if (m_condition.m_queue)
            ::CloseHandle(m_condition.m_queue);
        if (m_condition.m_mutex)
            ::CloseHandle(m_condition.m_mutex);
    }
}

ThreadCondition::~ThreadCondition()
{
    ::CloseHandle(m_condition.m_gate);
    ::CloseHandle(m_condition.m_queue);
    ::CloseHandle(m_condition.m_mutex);
}
    
void ThreadCondition::wait(Mutex& mutex)
{
    PlatformMutex& cs = mutex.impl();

    // Enter the wait state.
    DWORD res = ::WaitForSingleObject(m_condition.m_gate, INFINITE);
    ASSERT(res == WAIT_OBJECT_0);
    ++m_condition.m_blocked;
    res = ::ReleaseSemaphore(m_condition.m_gate, 1, 0);
    ASSERT(res);

    ::LeaveCriticalSection(&cs.m_internalMutex);

    res = ::WaitForSingleObject(m_condition.m_queue, INFINITE);
    ASSERT(res == WAIT_OBJECT_0);

    res = ::WaitForSingleObject(m_condition.m_mutex, INFINITE);
    ASSERT(res == WAIT_OBJECT_0);
    size_t wasWaiting = m_condition.m_waitingForRemoval;
    size_t wasTimedOut = m_condition.m_timedOut;
    if (wasWaiting != 0) {
        if (--m_condition.m_waitingForRemoval == 0) {
            if (m_condition.m_blocked != 0) {
                res = ::ReleaseSemaphore(m_condition.m_gate, 1, 0);  // open m_gate
                ASSERT(res);
                wasWaiting = 0;
            }
            else if (m_condition.m_timedOut != 0)
                m_condition.m_timedOut = 0;
        }
    } else if (++m_condition.m_timedOut == ((std::numeric_limits<unsigned>::max)() / 2)) {
        // timeout occured, normalize the m_condition.m_timedOut count
        // this may occur if many calls to wait with a timeout are made and
        // no call to notify_* is made
        res = ::WaitForSingleObject(m_condition.m_gate, INFINITE);
        ASSERT(res == WAIT_OBJECT_0);
        m_condition.m_blocked -= m_condition.m_timedOut;
        res = ::ReleaseSemaphore(m_condition.m_gate, 1, 0);
        ASSERT(res);
        m_condition.m_timedOut = 0;
    }
    res = ::ReleaseMutex(m_condition.m_mutex);
    ASSERT(res);

    if (wasWaiting == 1) {
        for (/**/ ; wasTimedOut; --wasTimedOut) {
            // better now than spurious later
            res = ::WaitForSingleObject(m_condition.m_queue, INFINITE);
            ASSERT(res == WAIT_OBJECT_0);
        }
        res = ::ReleaseSemaphore(m_condition.m_gate, 1, 0);
        ASSERT(res);
    }

    ::EnterCriticalSection (&cs.m_internalMutex);
}

bool ThreadCondition::timedWait(Mutex& mutex, double interval)
{
    // Empty for now
    ASSERT(false);
    return false;
}

void ThreadCondition::signal()
{
    unsigned signals = 0;

    DWORD res = ::WaitForSingleObject(m_condition.m_mutex, INFINITE);
    ASSERT(res == WAIT_OBJECT_0);

    if (m_condition.m_waitingForRemoval != 0) { // the m_gate is already closed
        if (m_condition.m_blocked == 0) {
            res = ::ReleaseMutex(m_condition.m_mutex);
            ASSERT(res);
            return;
        }

        ++m_condition.m_waitingForRemoval;
        --m_condition.m_blocked;

        signals = 1;
    } else {
        res = ::WaitForSingleObject(m_condition.m_gate, INFINITE);
        ASSERT(res == WAIT_OBJECT_0);
        if (m_condition.m_blocked > m_condition.m_timedOut) {
            if (m_condition.m_timedOut != 0) {
                m_condition.m_blocked -= m_condition.m_timedOut;
                m_condition.m_timedOut = 0;
            }
            signals = m_condition.m_waitingForRemoval = 1;
            --m_condition.m_blocked;
        } else {
            res = ::ReleaseSemaphore(m_condition.m_gate, 1, 0);
            ASSERT(res);
        }
    }

    res =::ReleaseMutex(m_condition.m_mutex);
    ASSERT(res);

    if (signals) {
        res = ::ReleaseSemaphore(m_condition.m_queue, signals, 0);
        ASSERT(res);
    }
}

void ThreadCondition::broadcast()
{
    unsigned signals = 0;

    WORD res = ::WaitForSingleObject(m_condition.m_mutex, INFINITE);
    ASSERT(res == WAIT_OBJECT_0);

    if (m_condition.m_waitingForRemoval != 0) { // the m_gate is already closed
        if (m_condition.m_blocked == 0) {
            res = ::ReleaseMutex(m_condition.m_mutex);
            ASSERT(res);
            return;
        }

        m_condition.m_waitingForRemoval += (signals = m_condition.m_blocked);
        m_condition.m_blocked = 0;
    } else {
        res = ::WaitForSingleObject(m_condition.m_gate, INFINITE);
        ASSERT(res == WAIT_OBJECT_0);
        if (m_condition.m_blocked > m_condition.m_timedOut) {
            if (m_condition.m_timedOut != 0) {
                m_condition.m_blocked -= m_condition.m_timedOut;
                m_condition.m_timedOut = 0;
            }
            signals = m_condition.m_waitingForRemoval = m_condition.m_blocked;
            m_condition.m_blocked = 0;
        } else {
            res = ::ReleaseSemaphore(m_condition.m_gate, 1, 0);
            ASSERT(res);
        }
    }

    res = ::ReleaseMutex(m_condition.m_mutex);
    ASSERT(res);

    if (signals) {
        res = ::ReleaseSemaphore(m_condition.m_queue, signals, 0);
        ASSERT(res);
    }
}

} // namespace WTF
