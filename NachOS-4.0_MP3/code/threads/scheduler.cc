// scheduler.cc 
//	Routines to choose the next thread to run, and to dispatch to
//	that thread.
//
// 	These routines assume that interrupts are already disabled.
//	If interrupts are disabled, we can assume mutual exclusion
//	(since we are on a uniprocessor).
//
// 	NOTE: We can't use Locks to provide mutual exclusion here, since
// 	if we needed to wait for a lock, and the lock was busy, we would 
//	end up calling FindNextToRun(), and that would put us in an 
//	infinite loop.
//
// 	Very simple implementation -- no priorities, straight FIFO.
//	Might need to be improved in later assignments.
//
// Copyright (c) 1992-1996 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "debug.h"
#include "scheduler.h"
#include "main.h"

//----------------------------------------------------------------------
// Scheduler::Scheduler
// 	Initialize the list of ready but not running threads.
//	Initially, no ready threads.
//----------------------------------------------------------------------


static int SJFCompare(Thread* a, Thread* b) {
  if(a->getBurstTime() == b->getBurstTime())
    return 0;
  else
    return a->getBurstTime() > b->getBurstTime() ? 1 : -1;
}

static int PriorityCompare(Thread* a, Thread* b) {
  if(a->getPriority() == b->getPriority())
    return 0;
  else
    return a->getPriority() < b->getPriority() ? 1 : -1;
}

Scheduler::Scheduler()
{ 
    blockedThread = new List<Thread *>; 
    readyList_L1 = new SortedList<Thread *>(SJFCompare);
    readyList_L2 = new SortedList<Thread *>(PriorityCompare);
    readyList_L3 = new List<Thread *>;
    toBeDestroyed = NULL;
} 

//----------------------------------------------------------------------
// Scheduler::~Scheduler
// 	De-allocate the list of ready threads.
//----------------------------------------------------------------------

Scheduler::~Scheduler()
{ 
    delete readyList_L1;
    delete readyList_L2;
    delete readyList_L3;   
} 

//----------------------------------------------------------------------
// Scheduler::ReadyToRun
// 	Mark a thread as ready, but not running.
//	Put it on the ready list, for later scheduling onto the CPU.
//
//	"thread" is the thread to be put on the ready list.
//----------------------------------------------------------------------

void
Scheduler::ReadyToRun (Thread *thread)
{
    int priority = thread->getPriority();
    int prev_Status = thread->getStatus();
    Thread *currentThread = kernel->currentThread;
  
    ASSERT(kernel->interrupt->getLevel() == IntOff);
    DEBUG(dbgThread, "Putting thread on ready list: " << thread->getName());
	
    thread->setStatus(READY);
    thread->setWaitTime(0);
    
    if(thread->getPriority() >= 100){ // put into L1
        readyList_L1->Insert(thread);
        cout << "Tick[" << kernel->stats->totalTicks << "]: "
             << "Thread[" << thread->getID() << "] is inserted into queue L[1]\n";
             
    } 
    else if(thread->getPriority() >= 50) {
      readyList_L2->Insert(thread);
      cout << "Tick[" << kernel->stats->totalTicks << "]: "
           << "Thread[" << thread->getID() << "] is inserted into queue L[2]\n";
    }
    else {
      readyList_L3->Append(thread);
      cout << "Tick[" << kernel->stats->totalTicks << "]: "
           << "Thread[" << thread->getID() << "] is inserted into queue L[3]\n";
    }
    // readyList->Append(thread);
}

//----------------------------------------------------------------------
// Scheduler::FindNextToRun
// 	Return the next thread to be scheduled onto the CPU.
//	If there are no ready threads, return NULL.
// Side effect:
//	Thread is removed from the ready list.
//----------------------------------------------------------------------

Thread *
Scheduler::FindNextToRun ()
{
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    if (readyList_L1->IsEmpty() && readyList_L2->IsEmpty() && readyList_L3->IsEmpty()) {
		    return NULL;
    } else {
      int schedulerType = getScheduleMode();
      
      
      switch (schedulerType) {
        case SJF:
          cout << "Tick[" << kernel->stats->totalTicks << "]: Thread["
               << readyList_L1->Front()->getID() << "] is removed from queue L[1]\n";
          return readyList_L1->RemoveFront();
        case Priority:
          cout << "Tick[" << kernel->stats->totalTicks << "]: Thread["
               << readyList_L2->Front()->getID() << "] is removed from queue L[2]\n";
          return readyList_L2->RemoveFront();
        default: 
          cout << "Tick[" << kernel->stats->totalTicks << "]: Thread["
               << readyList_L3->Front()->getID() << "] is removed from queue L[3]\n";
          return readyList_L3->RemoveFront();
      }
    }
}

//----------------------------------------------------------------------
// Scheduler::Run
// 	Dispatch the CPU to nextThread.  Save the state of the old thread,
//	and load the state of the new thread, by calling the machine
//	dependent context switch routine, SWITCH.
//
//      Note: we assume the state of the previously running thread has
//	already been changed from running to blocked or ready (depending).
// Side effect:
//	The global variable kernel->currentThread becomes nextThread.
//
//	"nextThread" is the thread to be put into the CPU.
//	"finishing" is set if the current thread is to be deleted
//		once we're no longer running on its stack
//		(when the next thread starts running)
//----------------------------------------------------------------------

void
Scheduler::Run (Thread *nextThread, bool finishing)
{
    Thread *oldThread = kernel->currentThread;
    
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    if (finishing) {	// mark that we need to delete current thread
       ASSERT(toBeDestroyed == NULL);
       toBeDestroyed = oldThread;
    }
    
    if (oldThread->space != NULL) {	// if this thread is a user program,
        oldThread->SaveUserState(); 	// save the user's CPU registers
		oldThread->space->SaveState();
    }
    
    oldThread->CheckOverflow();		    // check if the old thread
					    // had an undetected stack overflow
    
    kernel->currentThread = nextThread;  // switch to the next thread
    nextThread->setStatus(RUNNING);      // nextThread is now running
    
    DEBUG(dbgThread, "Switching from: " << oldThread->getName() << " to: " << nextThread->getName());
    
    // This is a machine-dependent assembly language routine defined 
    // in switch.s.  You may have to think
    // a bit to figure out what happens after this, both from the point
    // of view of the thread and from the perspective of the "outside world".
    SWITCH(oldThread, nextThread);

    // we're back, running oldThread
      
    // interrupts are off when we return from switch!
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    DEBUG(dbgThread, "Now in thread: " << oldThread->getName());

    CheckToBeDestroyed();		// check if thread we were running
					// before this one has finished
					// and needs to be cleaned up
    
    if (oldThread->space != NULL) {	    // if there is an address space
        oldThread->RestoreUserState();     // to restore, do it.
		oldThread->space->RestoreState();
    }
}

//----------------------------------------------------------------------
// Scheduler::CheckToBeDestroyed
// 	If the old thread gave up the processor because it was finishing,
// 	we need to delete its carcass.  Note we cannot delete the thread
// 	before now (for example, in Thread::Finish()), because up to this
// 	point, we were still running on the old thread's stack!
//----------------------------------------------------------------------

void
Scheduler::CheckToBeDestroyed()
{
    if (toBeDestroyed != NULL) {
        delete toBeDestroyed;
         toBeDestroyed = NULL;
    }
}
 
//----------------------------------------------------------------------
// Scheduler::Print
// 	Print the scheduler state -- in other words, the contents of
//	the ready list.  For debugging.
//----------------------------------------------------------------------
void
Scheduler::Print()
{
    cout << "Ready list contents:\n";
    readyList_L1->Apply(ThreadPrint);
    readyList_L2->Apply(ThreadPrint);
    readyList_L3->Apply(ThreadPrint);
}

int 
Scheduler::getScheduleMode()
{
  if(!readyList_L1->IsEmpty())
    return SJF;
  else if(!readyList_L2->IsEmpty())
    return Priority;
  else if(!readyList_L3->IsEmpty())
    return RR;
  else 
    return ERROR;
}

Thread* 
Scheduler::get_readyList_Front(int order, bool remove)
{
  if(!remove) {
    switch (order) {
      case 1:
        return readyList_L1->Front();
      case 2:
        return readyList_L2->Front();
      case 3:
        return readyList_L3->Front();
      default:
        return NULL;
    }
  } else {
    switch (order) {
      case 1:
        return readyList_L1->RemoveFront();
      case 2:
        return readyList_L2->RemoveFront();
      case 3:
        return readyList_L3->RemoveFront();
      default:
        return NULL;
    }
  }
}

void
Scheduler::Aging()
{
  int status = kernel->interrupt->getStatus();
  int priority, newPriority, waitTime;
  int tick_plus = (status == 1 ? 10 : 1);
  
  SortedList<Thread *> *tmp_L1 = new SortedList<Thread *>(SJFCompare);
  SortedList<Thread *> *tmp_L2 = new SortedList<Thread *>(PriorityCompare);
  List<Thread *> *tmp_L3 = new List<Thread *>;
  Thread *t, *currentThread = kernel->currentThread;
  

    while(!readyList_L1->IsEmpty()) {
      priority = readyList_L1->Front()->getPriority();
      waitTime = readyList_L1->Front()->getWaitTime();
      
      if(waitTime + tick_plus < 1500) {
        // cout << "Tick[" << kernel->stats->totalTicks << "]: Thread["
        //      << t->getName() << "] changes its waitTime"
        //      << " from [" << waitTime << "] to [" << waitTime + tick_plus << "]\n";
        
        t->setWaitTime(waitTime + tick_plus);
        tmp_L1->Insert(readyList_L1->RemoveFront());
      } else {
        newPriority = (priority <= 139? priority+10 : 149);
        
        readyList_L1->Front()->setWaitTime(0);
        
        cout << "Tick[" << kernel->stats->totalTicks << "]: Thread["
             << readyList_L1->Front()->getID() << "] changes its priority from [" << priority
             << "] to [" << newPriority << "]\n";
        readyList_L1->Front()->setPriority(newPriority);
        
        tmp_L1->Insert(readyList_L1->RemoveFront());
      }
    }
    readyList_L1 = tmp_L1;
    
    
    while(!readyList_L2->IsEmpty()) {
      priority = readyList_L2->Front()->getPriority();
      waitTime = readyList_L2->Front()->getWaitTime();
      
      if(waitTime + tick_plus < 1500) {
        // cout << "Tick[" << kernel->stats->totalTicks << "]: Thread["
        //      << readyList_L2->Front()->getName() << "] changes its waitTime"
        //      << " from [" << waitTime << "] to [" << waitTime + tick_plus << "]\n";
        // 
        readyList_L2->Front()->setWaitTime(waitTime + tick_plus);
        tmp_L2->Insert(readyList_L2->RemoveFront());
      } else {
        newPriority = (priority <= 139? priority+10 : 149);
        
        readyList_L2->Front()->setWaitTime(0);
        
        cout << "Tick[" << kernel->stats->totalTicks << "]: Thread["
             << readyList_L2->Front()->getID() << "] changes its priority from [" << priority
             << "] to [" << newPriority << "]\n";
        readyList_L2->Front()->setPriority(newPriority);

        if(priority >= 90) {
          t = readyList_L2->RemoveFront();
          cout << "Tick[" << kernel->stats->totalTicks << "]: Thread["
               << t->getID() << "] is removed from queue L[2]\n";
          
          readyList_L1->Insert(t);
          cout << "Tick[" << kernel->stats->totalTicks << "]: Thread["
               << t->getID() << "] is inserted into queue L[1]\n";
          
          if(currentThread->getPriority() >= 100 && currentThread->getBurstTime() > t->getBurstTime()) {
            kernel->interrupt->YieldOnReturn();
          } else if(currentThread->getPriority() >= 50) {
            kernel->interrupt->YieldOnReturn();
          }
          
        } else {
          t = readyList_L2->RemoveFront();
          tmp_L2->Insert(t);
        }
      }
    }
    readyList_L2 = tmp_L2;
    
    while(!readyList_L3->IsEmpty()) {
      priority = readyList_L3->Front()->getPriority();
      waitTime = readyList_L3->Front()->getWaitTime();
      
      if(waitTime + tick_plus < 1500) {
        // cout << "Tick[" << kernel->stats->totalTicks << "]: Thread["
        //      << readyList_L3->Front()->getName() << "] changes its waitTime"
        //      << " from [" << waitTime << "] to [" << waitTime + tick_plus << "]\n";
             
        readyList_L3->Front()->setWaitTime(waitTime + tick_plus);
        tmp_L3->Append(readyList_L3->RemoveFront());
      } else {
        newPriority = (priority <= 139? priority+10 : 149);
        
        readyList_L3->Front()->setWaitTime(0);
        
        cout << "Tick[" << kernel->stats->totalTicks << "]: Thread["
             << readyList_L3->Front()->getID() << "] changes its priority from [" << priority
             << "] to [" << newPriority << "]\n";
        readyList_L3->Front()->setPriority(newPriority);

        if(priority >= 40) {
          t = readyList_L3->RemoveFront();
          cout << "Tick[" << kernel->stats->totalTicks << "]: Thread["
               << t->getID() << "] is removed from queue L[3]\n";
          
          readyList_L2->Insert(t);
          cout << "Tick[" << kernel->stats->totalTicks << "]: Thread["
               << t->getID() << "] is inserted into queue L[2]\n";
               
          if(currentThread->getPriority() < 50) {
            kernel->interrupt->YieldOnReturn();
          }   
        } else {
          tmp_L3->Append(readyList_L3->Front());
          readyList_L3->RemoveFront();
        }
      }
    }
    readyList_L3 = tmp_L3;
      
}
void 
Scheduler::BlockThreadRemove()
{
  List<Thread *> *tmp_block = new List<Thread *>;
  Thread *t;
  
  while(!blockedThread->IsEmpty()) {
    t =  blockedThread->RemoveFront();
    
    if(t->getStatus() == BLOCKED) {
      tmp_block->Append(t);
    }
  }
  blockedThread = tmp_block;
}