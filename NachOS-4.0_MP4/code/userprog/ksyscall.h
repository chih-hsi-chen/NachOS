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

#ifndef FILESYS_STUB
int SysCreate(char *filename, int length)
{
	// return value
	// 1: success
	// 0: failed
	return kernel->interrupt->CreateFile(filename, length);
}
int SysOpen(char *filename)
{
	// return value
	// <=0: fail
	// > 0: success
	return kernel->interrupt->OpenFile(filename);
}
int SysClose(int id)
{
	// return value
	// 1: success
	// 0: failed
	return kernel->interrupt->CloseFile(id);
}
int SysWrite(char *buffer, int size, int id)
{
	return kernel->interrupt->WriteFile(buffer, size, id);
}
int SysRead(char *buffer, int size, int id)
{
	return kernel->interrupt->ReadFile(buffer, size, id);
}
#endif


#endif /* ! __USERPROG_KSYSCALL_H__ */
