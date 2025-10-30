/*******************************************************************************
# Copyright 2020 IoT.bzh
#
# author: Salma Raiss <salma.raiss@iot.bzh>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
*******************************************************************************/
#define _GNU_SOURCE

#include "wifi-ap-thread.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#define AFB_BINDING_VERSION 4
#include <afb/afb-binding.h>

#include "wifi-ap-utilities.h"

/*******************************************************************************
 * Pointer to  the head of threadList in service
 *******************************************************************************/
static struct cds_list_head threadList;

/*******************************************************************************
 * Key under which the pointer to the Thread Object (thread_Obj_t) will be
 * kept in thread-local storage.
 * This allows a thread to quickly get a pointer to its own Thread Object.
 *******************************************************************************/
static pthread_key_t ThreadLocalDataKey;

/*******************************************************************************
 * Mutex used to protect data structures within this module from multithreaded
 * race conditions.
 *******************************************************************************/
static pthread_mutex_t Mutex =
    PTHREAD_MUTEX_INITIALIZER;  // Pthreads FAST mutex.

static inline void lock()
{
    int rc = pthread_mutex_lock(&Mutex);
    assert(rc == 0);
}

static inline void unlock()
{
    int rc = pthread_mutex_unlock(&Mutex);
    assert(rc == 0);
}

/*******************************************************************************
 * Get Thread struct from Global thread list using the thread id
 *******************************************************************************/

thread_Obj_t *findThreadFromIdInList(struct cds_list_head *listHead, int id)
{
    thread_Obj_t *threadPtr;

    cds_list_for_each_entry(threadPtr, listHead, link)
    {
        // check for thread with the same id
        if (threadPtr->threadId == id)
            return threadPtr;
    }
    return NULL;
}

/*******************************************************************************
 * Make a thread JOINABLE
 *******************************************************************************
 * i.e: When the thread finishes, it will remain in existence until another
 * thread "joins" with it by calling threadJoin().
 * By default, threads are not joinable and will be destructed
 * automatically when they finish.
 *
 * @return
 *   - -1 : if thread Id provided is invalid
 *   - -2 : if the thread with id provided provided is already running
 *******************************************************************************/
int setThreadJoinable(int threadId)
{
    lock();

    thread_Obj_t *threadPtr = findThreadFromIdInList(&threadList, threadId);

    unlock();

    // if invalid thread reference
    if (!threadPtr)
        return -1;

    // if thread already running
    if (threadPtr->state != THREAD_STATE_NEW)
        return -2;

    threadPtr->isJoinable = true;
    if (pthread_attr_setdetachstate(&(threadPtr->attr),
                                    PTHREAD_CREATE_JOINABLE) == 0)
        return 0;
    else
        return -3;
}

/*******************************************************************************
 * Create a new Thread object and initializes it
 *******************************************************************************
 * @return A reference to the thread (doesn't return if failed).
 *
 * @warning This function will also be called for the process's main thread
 * by the processes * main thread.  Keep that in mind when modifying this
 *function.
 *******************************************************************************/

thread_Obj_t *CreateThread(
    const char *name,            ///< [in] Name of the thread
    thread_MainFunc_t mainFunc,  ///< [in] The thread's main function.
    void *context  ///< [in] Value to pass to mainFunc when it is called.
)
{
    // Create a new thread object.
    thread_Obj_t *threadPtr = (thread_Obj_t *)calloc(1, sizeof(thread_Obj_t));

    // And zero the whole object.
    memset(threadPtr, 0, sizeof(thread_Obj_t));

    // Get current thread as we may inherit some properties (if available).
    // Do not use  GetCurrentThreadPtr() as it's OK if this thread is being
    // created from a non-Legato thread; in that case we just use default
    // values.

    thread_Obj_t *currentThreadPtr = pthread_getspecific(ThreadLocalDataKey);

    // Initialize the pthreads attribute structure.
    int rc = pthread_attr_init(&(threadPtr->attr));
    assert(rc == 0);

    // Copy the name.  We will make the names unique by adding the thread ID
    // later so we allow any string as the name.
    if (utf8_Copy(threadPtr->name, name, sizeof(threadPtr->name), NULL)) {
        AFB_WARNING("Thread name %s has been truncated to %s", name,
                    threadPtr->name);
    }

    // Make sure when we create the thread it takes it attributes from the
    // attribute object, as opposed to inheriting them from its parent thread.
    if (pthread_attr_setinheritsched(&(threadPtr->attr),
                                     PTHREAD_EXPLICIT_SCHED) != 0) {
        AFB_ERROR(
            "Could not set scheduling policy inheritance for thread '%s'.",
            name);
        return NULL;
    }

    // By default, threads are not joinable (they are detached).
    if (pthread_attr_setdetachstate(&(threadPtr->attr),
                                    PTHREAD_CREATE_DETACHED) != 0) {
        AFB_ERROR("Could not set the detached state for thread '%s'.", name);
        return NULL;
    }

    threadPtr->priority = DEFAULT_THREAD_PRIORITY;
    threadPtr->isJoinable = false;
    threadPtr->state = THREAD_STATE_NEW;
    threadPtr->mainFunc = mainFunc;
    threadPtr->context = context;
    threadPtr->threadHandle = 0;
    threadPtr->setPidOnStart = false;
    threadPtr->procId = 0;

    CDS_INIT_LIST_HEAD(&threadPtr->destructorList);

    if (currentThreadPtr) {
        if (currentThreadPtr->procId != 0) {
            threadPtr->procId = currentThreadPtr->procId;
        }
    }

    // Create a safe reference for this object and put this object on the
    // thread object list (for the Inspect tool).
    lock();
    CDS_INIT_LIST_HEAD(&threadList);
    CDS_INIT_LIST_HEAD(&threadPtr->link);
    threadPtr->ThreadObjListChangeCount++;
    threadPtr->threadId++;
    cds_list_add_tail(&threadPtr->link, &threadList);
    unlock();

    AFB_INFO("DONE Creating thread");

    return threadPtr;
}

/*******************************************************************************
 * Add destructor object to a given thread's Destructor List
 *******************************************************************************/
static Destructor_t *AddDestructor(thread_Obj_t *threadPtr,
                                   thread_Destructor_t destructor,
                                   void *context)
{
    // Create the destructor object.
    Destructor_t *destructorObjPtr =
        (Destructor_t *)calloc(1, sizeof(Destructor_t));

    // Init the destructor object.
    CDS_INIT_LIST_HEAD(&destructorObjPtr->link);
    destructorObjPtr->threadPtr = threadPtr;
    destructorObjPtr->destructor = destructor;
    destructorObjPtr->context = context;

    // Get a pointer to the calling thread's Thread Object and
    // Add the destructor object to its list.
    cds_list_add_tail(&destructorObjPtr->link, &threadPtr->destructorList);

    AFB_INFO("Done adding destructor to thread");

    return destructorObjPtr;
}

/*******************************************************************************
 * Register a destructor function for a child thread
 *******************************************************************************
 * The destructor will be called by the child thread just before it terminates.
 * This can only be *done before the child * thread is started.
 * After that, only the child thread can add its own destructors.
 *******************************************************************************/
int addDestructorToThread(int threadId,
                          thread_Destructor_t destructor,
                          void *context)
{
    // Get a pointer to the thread's Thread Object.
    lock();

    thread_Obj_t *threadPtr = findThreadFromIdInList(&threadList, threadId);

    unlock();

    // if invalid thread reference
    if (!threadPtr)
        return -1;

    // if thread already running
    if (threadPtr->state != THREAD_STATE_NEW)
        return -2;

    AddDestructor(threadPtr, destructor, context);
    AFB_INFO("Done registring a destructor to thread");
    return 0;
}

/*******************************************************************************
 * Delete a thread object
 *******************************************************************************/
static void DeleteThread(thread_Obj_t *threadPtr)
{
    // Destruct the thread attributes structure.
    pthread_attr_destroy(&(threadPtr->attr));

    // Release the Thread object
    free(threadPtr);
}

int GetNumberOfNodesInList(struct cds_list_head *listHead)
{
    int numberOfNodes = 0;
    struct cds_list_head *listNode;

    if (!listHead)
        return -1;

    if (cds_list_empty(listHead))
        return 0;

    cds_list_for_each(listNode, listHead) numberOfNodes++;

    return numberOfNodes;
}

/*******************************************************************************
 * Clean-up function that gets run by a thread just before it dies
 *******************************************************************************/
static void CleanupThread(void *objPtr)
{
    thread_Obj_t *threadObjPtr = objPtr;
    // Get the destructor object
    Destructor_t *destructorObjPtr;

    threadObjPtr->state = THREAD_STATE_DYING;

    cds_list_for_each_entry(destructorObjPtr, &threadObjPtr->destructorList,
                            link)
    {
        // Call the destructor.
        if (destructorObjPtr->destructor != NULL) {
            // WARNING: This may change the destructor list (by deleting a
            // destructor).
            destructorObjPtr->destructor(destructorObjPtr->context);
        }
        cds_list_del(&destructorObjPtr->link);
        free(destructorObjPtr);
        if (!GetNumberOfNodesInList(&threadObjPtr->destructorList))
            break;
    }

    // If this thread is NOT joinable, then immediately invalidate its safe
    // reference, remove it from the thread object list, and free the thread
    // object. Otherwise, wait until someone joins with it.
    if (!threadObjPtr->isJoinable) {
        lock();
        cds_list_del(&threadObjPtr->link);
        unlock();
        DeleteThread(threadObjPtr);
    }

    // Clear the thread info to prevent double-free errors and further thread
    // calls. Check if the key exists before cleaning it up
    void *current_data = pthread_getspecific(ThreadLocalDataKey);
    if (current_data != NULL) {
        int result = pthread_setspecific(ThreadLocalDataKey, NULL);
        if (result != 0) {
            fprintf(stderr, "Warning: Failed to clear thread-local data: %s\n",
                    strerror(result));
        }
    }
    // Otherwise, data is already NULL, no need to clean it up
}

/*******************************************************************************
 * A pthread start routine function wrapper
 *******************************************************************************
 *   We pass this function to the created pthread and we pass the thread object
 *   as a parameter to this function.
 *   This function then calls the user's main function. We do this because the
 *   user's main function has a different format then the start routine that
 *   pthread expects.
 *******************************************************************************/

void *PThreadStartRoutine(void *threadObjPtr)
{
    void *returnValue = NULL;
    thread_Obj_t *threadPtr = threadObjPtr;

    // Set the thread name (will be truncated to the platform-dependent name
    // buffer size).

    int result;

    if ((result = pthread_setname_np(threadPtr->threadHandle,
                                     threadPtr->name)) != 0) {
        AFB_WARNING("Failed to set thread name for %s (%d).", threadPtr->name,
                    result);
    }

    // Push the default destructor onto the thread's cleanup stack.
    pthread_cleanup_push(CleanupThread, threadPtr);

    // Set scheduler and nice value now, if thread is not a realtime thread.
    // Real-time thread priorities are set before thread is started.
#if !LE_CONFIG_THREAD_REALTIME_ONLY
    // If the thread is supposed to run in the background (at IDLE priority),
    // then switch to that scheduling policy now.

    if (threadPtr->priority == 0) {
        struct sched_param param;
        memset(&param, 0, sizeof(param));
        if (sched_setscheduler(0, SCHED_IDLE, &param) != 0) {
            AFB_ERROR(
                "Failed to set scheduling policy to SCHED_IDLE (error %d).",
                errno);
        }
        else {
            AFB_DEBUG("Set scheduling policy to SCHED_IDLE");
        }
    }
    else if ((threadPtr->priority == 2) || (threadPtr->priority == 1) ||
             (threadPtr->priority == 3)) {
        int niceLevel = 0;

        if (threadPtr->priority == 1) {
            niceLevel = 10;
        }
        else if (threadPtr->priority == 3) {
            niceLevel = -10;
        }

        // Get this thread's tid.
        pid_t tid = gettid();

        errno = 0;
        if (setpriority(PRIO_PROCESS, (id_t)tid, niceLevel) == -1) {
            AFB_ERROR("Could not set the nice level (error %d).", errno);
        }
        else {
            AFB_DEBUG("Set nice level to %d.", niceLevel);
        }
    }

#endif /* end !LE_CONFIG_THREAD_REALTIME_ONLY */
    // Set the thread ID as a proc ID if configured

    if (threadPtr->setPidOnStart) {
        threadPtr->procId = (pid_t)threadPtr->threadHandle;
    }

    // Call the user's main function.
    returnValue = threadPtr->mainFunc(threadPtr->context);
    // Pop the default destructor and call it.
    pthread_cleanup_pop(1);

    return returnValue;
}

/*******************************************************************************
 * "Joins" the calling thread with another thread
 *******************************************************************************
 * "Joins" the calling thread with another thread.
 * Blocks the calling thread until the other  thread finishes.
 * After a thread has been joined with, its thread id is no longer valid and
 *must never be used again.
 *
 * @return
 *      -  0 if successful.
 *      - -1 if the other thread doesn't exist.
 *      - -2 if the other thread can't be joined with.
 *      - -3 if a thread tries to join with itself or two threads try to join
 *each other.
 *
 * @warning The other thread must be "joinable".  See setThreadJoinable();
 *
 * @warning It is an error for two or more threads try to join with the same
 *thread.
 ******************************************************************************/
int JoinThread(int threadId, void **resultValuePtr)
{
    int error;

    lock();

    thread_Obj_t *threadPtr = findThreadFromIdInList(&threadList, threadId);
    if (threadPtr == NULL) {
        unlock();

        AFB_ERROR("Attempt to join with non-existent thread (ref = %d).",
                  threadId);
        return -1;
    }
    else {
        pthread_t pthreadHandle = threadPtr->threadHandle;
        bool isJoinable = threadPtr->isJoinable;

        unlock();

        if (!isJoinable) {
            AFB_ERROR("Attempt to join with non-joinable thread '%s'.",
                      threadPtr->name);
            return -2;
        }
        else {
            error = pthread_join(pthreadHandle, resultValuePtr);

            switch (error) {
            case 0:
                // If the join was successful, it's time to delete the safe
                // reference, remove it from the list of thread objects, and
                // release the Thread Object.
                lock();
                threadPtr->ThreadObjListChangeCount++;
                cds_list_del(&threadPtr->link);
                unlock();
                DeleteThread(threadPtr);

                return 0;

            case EDEADLK:
                return -3;

            case ESRCH:
                return -1;

            default:
                AFB_ERROR("Unexpected return code from pthread_join(): %d",
                          error);
                return -2;
            }
        }
    }
}

/*******************************************************************************
 * Terminate a  thread of execution
 *******************************************************************************
 * Tells another thread to terminate.
 * This function returns immediately but the termination of the thread happens
 * asynchronously and is not guaranteed to occur when this function returns.
 *
 * @return
 *      -  0 if successful.
 *      - -1 if the thread doesn't exist.
 *******************************************************************************/
int cancelThread(int threadId)
{
    lock();
    thread_Obj_t *threadPtr = findThreadFromIdInList(&threadList, threadId);

    if ((threadPtr == NULL) || (pthread_cancel(threadPtr->threadHandle) != 0)) {
        AFB_ERROR("Can't cancel thread: thread doesn't exist!");
        return -1;
    }
    unlock();

    return 0;
}

/*******************************************************************************
 * Start a new thread of execution
 *******************************************************************************
 * After creating the thread, you have the opportunity to set attributes
 * before it starts.
 * It won't start until thread_Start() is called.
 *******************************************************************************/
int startThread(int threadId)
{
    // Get a pointer to the thread's Thread Object.
    lock();
    thread_Obj_t *threadPtr = findThreadFromIdInList(&threadList, threadId);

    unlock();

    // if invalid thread reference
    if (!threadPtr)
        return -1;

    // if thread already running
    if (threadPtr->state != THREAD_STATE_NEW)
        return -2;

    // Start the thread with the default function PThreadStartRoutine, passing
    // the PThreadStartRoutine the thread object. PThreadStartRoutine will then
    // start the user's main function.

    threadPtr->state = THREAD_STATE_RUNNING;

    int result = pthread_create(&(threadPtr->threadHandle), &(threadPtr->attr),
                                PThreadStartRoutine, threadPtr);

    if (result != 0) {
        errno = result;
        AFB_ERROR("pthread_create() failed with error code %d.", result);
        if (result == EPERM) {
            AFB_ERROR(
                "Insufficient permissions to create thread '%s' with its "
                "current attributes.",
                threadPtr->name);
        }
        else {
            AFB_ERROR("Failed to create thread '%s'.", threadPtr->name);
        }
        return result;
    }

    return 0;
}
