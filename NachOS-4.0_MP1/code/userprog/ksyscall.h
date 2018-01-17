/**************************************************************
 *
 * userprog/ksyscall.h
 *
 * Kernel interface for systemcalls 
 *
 * by Marcus Voelp  (c) Universitaet Karlsruhe
 *
 **************************************************************/

#ifndef __USERPROG_KSYSCALL_H__ 
#define __USERPROG_KSYSCALL_H__ 

#include "kernel.h"

#include "synchconsole.h"


void SysHalt()
{
  kernel->interrupt->Halt();
}

int SysAdd(int op1, int op2)
{
  return op1 + op2;
}

int SysCreate(char *filename)
{
	// return value
	// 1: success
	// 0: failed
	return kernel->interrupt->CreateFile(filename);
}

void SysPrintInt(int number)
{
  kernel->interrupt->PrintInt(number);
}

int SysOpen(char *filename) {
  //return value
  // >0: success
  // <=0: failed
  return kernel->interrupt->OpenFile(filename);
}

int SysClose(int openfileId) {
  //return value:
  // 1: success closed
  // 0: fail to close
  return kernel->interrupt->CloseFile(openfileId);
}

int SysWrite(char *buffer, int size, int id) {
  //return value:
  // number of characters actually written into file
  return kernel->interrupt->WriteFile(buffer, size, id);
}
char* SysRead(int size, int id)
{
	return kernel->interrupt->ReadFile(size, id);
}
#endif /* ! __USERPROG_KSYSCALL_H__ */
