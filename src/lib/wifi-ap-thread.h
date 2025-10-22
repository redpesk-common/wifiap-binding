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
#ifndef THREAD_HEADER_FILE
#define THREAD_HEADER_FILE

#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>
#include <urcu/list.h>

//----------------------------------------------------------------------------------------------------------------------
/**
 * Maximum thread name size in bytes.
 **/
//----------------------------------------------------------------------------------------------------------------------
#define MAX_THREAD_NAME_SIZE 24

#define DEFAULT_THREAD_PRIORITY 0;

//----------------------------------------------------------------------------------------------------------------------
/**
 * Main functions for threads must look like this:
 *
 * @param   context [IN] Context value that was passed to threadCreate().
 *
 * @return  Thread result value. If the thread is joinable, then this value can be obtained by
 *          another thread through a call to vt_thread_Join().  Otherwise, the return value is
 * ignored.
 */
//----------------------------------------------------------------------------------------------------------------------
typedef void *(*thread_MainFunc_t)(void *context  ///< See parameter documentation above.
);

//--------------------------------------------------------------------------------------------------
/**
 * Destructor functions for threads must look like this:
 *
 * @param context [IN] Context parameter that was passed into le_thread_SetDestructor() when
 *                      this destructor was registered.
 */
//--------------------------------------------------------------------------------------------------
typedef void (*thread_Destructor_t)(
    void *context  ///< [IN] Context parameter that was passed into le_thread_SetDestructor() when
                   ///       this destructor was registered.
);

//----------------------------------------------------------------------------------------------------------------------
/**
 * The binding thread structure containing all of the thread's attributes.
 **/
//----------------------------------------------------------------------------------------------------------------------
typedef struct thread_Obj
{
    struct cds_list_head link;
    int threadId;
    char name[MAX_THREAD_NAME_SIZE];  ///< The name of the thread.
    pthread_attr_t attr;              ///< The thread's attributes.
    int priority;                     ///< The thread's priority.
    bool isJoinable;                  ///< true = the thread is joinable, false = detached.

    /// Thread state.
    enum {
        THREAD_STATE_NEW,      ///< Not yet started.
        THREAD_STATE_RUNNING,  ///< Has been started.
        THREAD_STATE_DYING     /// Is in the process of cleaning up.
    } state;

    thread_MainFunc_t mainFunc;           ///< The main function for the thread.
    void *context;                        ///< Context value to be passed to mainFunc.
    struct cds_list_head destructorList;  ///< The destructor list for this thread.
    pthread_t threadHandle;               ///< The pthreads thread handle.
    size_t ThreadObjListChangeCount;
    bool setPidOnStart;  ///< Set PID on start flag
    pid_t procId;        ///< The main process ID for this thread
} thread_Obj_t;

//--------------------------------------------------------------------------------------------------
/**
 * The destructor object that can be added to a destructor list.  Used to hold user destructors.
 */
//--------------------------------------------------------------------------------------------------
typedef struct le_thread_Destructor
{
    struct cds_list_head link;       ///< A link in the thread's list of destructors.
    thread_Obj_t *threadPtr;         ///< Pointer to the thread this destructor is attached to.
    thread_Destructor_t destructor;  ///< The destructor function.
    void *context;                   ///< The context to pass to the destructor function.
} Destructor_t;

//**********************************************************************************************************************

// thread functions

int setThreadJoinable(int threadId);
int startThread(int threadId);
int addDestructorToThread(int threadId, thread_Destructor_t destructor, void *context);
thread_Obj_t *CreateThread(const char *name, thread_MainFunc_t mainFunc, void *context);
int GetNumberOfNodesInList(struct cds_list_head *listHead);
int JoinThread(int threadId, void **resultValuePtr);
int cancelThread(int threadId);

#endif
