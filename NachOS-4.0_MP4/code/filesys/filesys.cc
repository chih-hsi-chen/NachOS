// filesys.cc 
//	Routines to manage the overall operation of the file system.
//	Implements routines to map from textual file names to files.
//
//	Each file in the file system has:
//	   A file header, stored in a sector on disk 
//		(the size of the file header data structure is arranged
//		to be precisely the size of 1 disk sector)
//	   A number of data blocks
//	   An entry in the file system directory
//
// 	The file system consists of several data structures:
//	   A bitmap of free disk sectors (cf. bitmap.h)
//	   A directory of file names and file headers
//
//      Both the bitmap and the directory are represented as normal
//	files.  Their file headers are located in specific sectors
//	(sector 0 and sector 1), so that the file system can find them 
//	on bootup.
//
//	The file system assumes that the bitmap and directory files are
//	kept "open" continuously while Nachos is running.
//
//	For those operations (such as Create, Remove) that modify the
//	directory and/or bitmap, if the operation succeeds, the changes
//	are written immediately back to disk (the two files are kept
//	open during all this time).  If the operation fails, and we have
//	modified part of the directory and/or bitmap, we simply discard
//	the changed version, without writing it back to disk.
//
// 	Our implementation at this point has the following restrictions:
//
//	   there is no synchronization for concurrent accesses
//	   files have a fixed size, set when the file is created
//	   files cannot be bigger than about 3KB in size
//	   there is no hierarchical directory structure, and only a limited
//	     number of files can be added to the system
//	   there is no attempt to make the system robust to failures
//	    (if Nachos exits in the middle of an operation that modifies
//	    the file system, it may corrupt the disk)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.
#ifndef FILESYS_STUB

#include "copyright.h"
#include "debug.h"
#include "disk.h"
#include "directory.h"
#include "pbitmap.h"
#include "filehdr.h"
#include "filesys.h"

// Sectors containing the file headers for the bitmap of free sectors,
// and the directory of files.  These file headers are placed in well-known 
// sectors, so that they can be located on boot-up.
#define FreeMapSector 		0
#define DirectorySector 	1

// Initial file sizes for the bitmap and directory; until the file system
// supports extensible files, the directory size sets the maximum number 
// of files that can be loaded onto the disk.
#define FreeMapFileSize 	(NumSectors / BitsInByte)
#define NumDirEntries 		64
#define DirectoryFileSize 	(sizeof(DirectoryEntry) * NumDirEntries)

//----------------------------------------------------------------------
// FileSystem::FileSystem
// 	Initialize the file system.  If format = TRUE, the disk has
//	nothing on it, and we need to initialize the disk to contain
//	an empty directory, and a bitmap of free sectors (with almost but
//	not all of the sectors marked as free).  
//
//	If format = FALSE, we just have to open the files
//	representing the bitmap and the directory.
//
//	"format" -- should we initialize the disk?
//----------------------------------------------------------------------

FileSystem::FileSystem(bool format)
{ 
    DEBUG(dbgFile, "Initializing the file system.");
    if (format) {
        PersistentBitmap *freeMap = new PersistentBitmap(NumSectors);
        Directory *directory = new Directory(NumDirEntries);
		FileHeader *mapHdr = new FileHeader;
		FileHeader *dirHdr = new FileHeader;

        DEBUG(dbgFile, "Formatting the file system.");

		// First, allocate space for FileHeaders for the directory and bitmap
		// (make sure no one else grabs these!)
		freeMap->Mark(FreeMapSector);	    
		freeMap->Mark(DirectorySector);

		// Second, allocate space for the data blocks containing the contents
		// of the directory and bitmap files.  There better be enough space!

		ASSERT(mapHdr->Allocate(freeMap, FreeMapFileSize));
		ASSERT(dirHdr->Allocate(freeMap, DirectoryFileSize));

		// Flush the bitmap and directory FileHeaders back to disk
		// We need to do this before we can "Open" the file, since open
		// reads the file header off of disk (and currently the disk has garbage
		// on it!).

        DEBUG(dbgFile, "Writing headers back to disk.");
		mapHdr->WriteBack(FreeMapSector);    
		dirHdr->WriteBack(DirectorySector);

		// OK to open the bitmap and directory files now
		// The file system operations assume these two files are left open
		// while Nachos is running.

        freeMapFile = new OpenFile(FreeMapSector);
        directoryFile = new OpenFile(DirectorySector);
     
		// Once we have the files "open", we can write the initial version
		// of each file back to disk.  The directory at this point is completely
		// empty; but the bitmap has been changed to reflect the fact that
		// sectors on the disk have been allocated for the file headers and
		// to hold the file data for the directory and bitmap.

        DEBUG(dbgFile, "Writing bitmap and directory back to disk.");
		freeMap->WriteBack(freeMapFile);	 // flush changes to disk
		directory->WriteBack(directoryFile);

		if (debug->IsEnabled('f')) {
			freeMap->Print();
			directory->Print();
        }
        delete freeMap; 
		delete directory; 
		delete mapHdr; 
		delete dirHdr;
    } else {
		// if we are not formatting the disk, just open the files representing
		// the bitmap and directory; these are left open while Nachos is running
        freeMapFile = new OpenFile(FreeMapSector);
        directoryFile = new OpenFile(DirectorySector);
    }
}

//----------------------------------------------------------------------
// MP4 mod tag
// FileSystem::~FileSystem
//----------------------------------------------------------------------
FileSystem::~FileSystem()
{
	delete freeMapFile;
	delete directoryFile;
}

//----------------------------------------------------------------------
// FileSystem::Create
// 	Create a file in the Nachos file system (similar to UNIX create).
//	Since we can't increase the size of files dynamically, we have
//	to give Create the initial size of the file.
//
//	The steps to create a file are:
//	  Make sure the file doesn't already exist
//    Allocate a sector for the file header
// 	  Allocate space on disk for the data blocks for the file
//	  Add the name to the directory
//	  Store the new file header on disk 
//	  Flush the changes to the bitmap and the directory back to disk
//
//	Return TRUE if everything goes ok, otherwise, return FALSE.
//
// 	Create fails if:
//   		file is already in directory
//	 	no free space for file header
//	 	no free entry for file in directory
//	 	no free space for data blocks for the file 
//
// 	Note that this implementation assumes there is no concurrent access
//	to the file system!
//
//	"name" -- name of file to be created
//	"initialSize" -- size of file to be created
//----------------------------------------------------------------------

bool
FileSystem::Create(char *name, int initialSize)
{
    Directory *directory;
    PersistentBitmap *freeMap;
    FileHeader *hdr;
	OpenFile *openDirectoryFile;
    int sector, count = 0;
    bool success;
	char folder[10][10];

    DEBUG(dbgFile, "Creating file " << name << " size " << initialSize);

    openDirectoryFile = Parse(name, TRUE, folder, &count);
	
	
	/*cout << "Creating file: " << name << "\n";
	cout << "Level of path: " << count-1 << "\n";
	cout << "Path file: " << folder[count-1] << "\n";*/
	
	if(openDirectoryFile == NULL) {
		printf("No such directory.\n");
		success = FALSE;
		
	} else {
		directory = new Directory(NumDirEntries);
		directory->FetchFrom(openDirectoryFile);
		
		if (directory->Find(folder[count-1]) != -1) {
			success = FALSE;			// file is already in directory
			cout << "file is already in directory!!!\n";
		}
		else {	
			freeMap = new PersistentBitmap(freeMapFile,NumSectors);
			sector = freeMap->FindAndSet();	// find a sector to hold the file header
			if (sector == -1) {
				success = FALSE;		// no free block for file header 
				cout << "no free block for file header!!!.\n";
			}	
			else if (!directory->Add(folder[count-1], sector, FALSE)) {
				success = FALSE;	// no space in directory
				cout << "no space in directory.\n";
			}
				
			else {
				hdr = new FileHeader;
				if (!hdr->Allocate(freeMap, initialSize)) {
					success = FALSE;	// no space on disk for data
					cout << "no space on disk for data!!!.\n";
				}	
				else {	
					success = TRUE;
					// everthing worked, flush all changes back to disk
					hdr->WriteBack(sector); 		
					directory->WriteBack(openDirectoryFile);
					freeMap->WriteBack(freeMapFile);
				}
				delete hdr;
			}
			delete freeMap;
		}
		delete openDirectoryFile;
		delete directory;
	}
	
	
    return success;
}

//----------------------------------------------------------------------
// FileSystem::CreateDirectory
//  Create a new directory in Nachos File System
//  We just create a directory that has a fixed size
//  Be equal to sizeof(DirectoryEntry) * NumDirEntries
//  The steps to create new directory:
//    -Parse the string (name)
//    -Go to the bottom directory
//    -Allocate a sector for file header
//    -Add this new directory into original bottom directory
//    -Allocate space on disk for data blocks for directory
//    -Flush changes to the bitmap and directory back to disk
//----------------------------------------------------------------------
bool
FileSystem::CreateDirectory(char *path)
{
	char folder[10][10];
	char *cut = "/";
	char *pch;
	int sector, NewDirSector;
	int count = 0;
	bool success = TRUE;
	
	Directory *directory = new Directory(NumDirEntries);
	Directory *NewDirectory = new Directory(NumDirEntries);
	OpenFile *tempDirectory = new OpenFile(DirectorySector);
	OpenFile *NewDirectoryFile;
	PersistentBitmap *freeMap;
    FileHeader *hdr;
	
	directory->FetchFrom(directoryFile);
	pch = strtok(path, cut); // first cut
	
	
	while(pch != NULL) {
		strncpy(folder[count++], pch, FileNameMaxLen);
		pch = strtok(NULL, cut);
	}
	
	for(int i=0; i < count-1; i++) {
		if((sector = directory->Find(folder[i])) == -1) {
			success = FALSE;
			break;
		}
		
		if(tempDirectory != NULL) {
			delete tempDirectory;
			tempDirectory = NULL;
		}
		tempDirectory = new OpenFile(sector);
		directory->FetchFrom(tempDirectory);
	}
	
	freeMap = new PersistentBitmap(freeMapFile,NumSectors);
	
	if(!success) {
		printf("No such directory.\n");
		
	} else if((NewDirSector = freeMap->FindAndSet()) == -1) {
		printf("no free block for file header!!!.\n");
		success = FALSE;
	} else if(!directory->Add(folder[count-1], NewDirSector, TRUE)) {
		printf("no space in directory.\n");
		success = FALSE;
	} else {
		hdr = new FileHeader;
		
		if(!hdr->Allocate(freeMap, DirectoryFileSize)) {
			printf("no space on disk for data!!!.\n");
			success = FALSE;
		} else {
			success = TRUE;
			// everything is done!!!
			hdr->WriteBack(NewDirSector);
			
			NewDirectoryFile = new OpenFile(NewDirSector);
			NewDirectory->WriteBack(NewDirectoryFile);
			
			directory->WriteBack(tempDirectory);
			freeMap->WriteBack(freeMapFile);
			
			delete NewDirectoryFile;
		}
		delete hdr;
	}
	delete freeMap;
	delete tempDirectory;
	delete directory;
	delete NewDirectory;
	
	return success;
}


// FileSystem::Open
// 	Open a file for reading and writing.  
//	To open a file:
//	  Find the location of the file's header, using the directory 
//	  Bring the header into memory
//
//	"name" -- the text name of the file to be opened
//----------------------------------------------------------------------

OpenFile *
FileSystem::Open(char *name)
{ 
    Directory *directory = new Directory(NumDirEntries);
    OpenFile *openFile = NULL;
	OpenFile *openDirectoryFile = NULL;
    int sector, count = 0;
	char folder[10][10];

    DEBUG(dbgFile, "Opening file" << name);
	
	openDirectoryFile = Parse(name, TRUE, folder, &count);
	
	if(openDirectoryFile != NULL) {
		directory->FetchFrom(openDirectoryFile);
		sector = directory->Find(folder[count-1]); 
		if (sector >= 0) 		
			openFile = new OpenFile(sector);	// name was found in directory 
	}
	
    delete directory;
	delete openDirectoryFile;
    return openFile;				// return NULL if not found
}

//----------------------------------------------------------------------
// FileSystem::Close
//   Close an opened file
//   
//   "id" -- the file identity of opened file
//   
//   if id<=0, then we can't close it because it doesn't exist.  return 0.
//   else close the file and return 1.
//----------------------------------------------------------------------

int 
FileSystem::Close(int id)
{
	OpenFile *openFile = NULL;
	
	if(id <= 0)
		return 0;

	openFile = (OpenFile*)(id);
	
	if(openFile == NULL)
		return 0;
	
	delete openFile;
	openFile = NULL;
	return 1;
}

//----------------------------------------------------------------------
// FileSystem::Remove
// 	Delete a file from the file system.  This requires:
//	    Remove it from the directory
//	    Delete the space for its header
//	    Delete the space for its data blocks
//	    Write changes to directory, bitmap back to disk
//
//	Return TRUE if the file was deleted, FALSE if the file wasn't
//	in the file system.
//
//	"name" -- the text name of the file to be removed
//----------------------------------------------------------------------

bool
FileSystem::Remove(char *name)
{ 
    Directory *directory;
    PersistentBitmap *freeMap;
    FileHeader *fileHdr;
	OpenFile *openDirectoryFile = NULL;
    int sector, count = 0;
	char folder[10][10];
    
    directory = new Directory(NumDirEntries);
	openDirectoryFile = Parse(name, TRUE, folder, &count);
	
	if(openDirectoryFile == NULL) {
		printf("No such directory.\n");
		delete directory;
		return FALSE;
	} else {
		directory->FetchFrom(openDirectoryFile);
	
		sector = directory->Find(folder[count-1]);
		if (sector == -1) {
		   delete directory;
		   delete openDirectoryFile;
		   printf("No such file\n");
		   return FALSE;			 // file not found 
		}
		fileHdr = new FileHeader;
		fileHdr->FetchFrom(sector);

		freeMap = new PersistentBitmap(freeMapFile,NumSectors);

		fileHdr->Deallocate(freeMap);  		// remove data blocks
		freeMap->Clear(sector);			// remove header block
		directory->Remove(folder[count-1]);

		freeMap->WriteBack(freeMapFile);		// flush to disk
		directory->WriteBack(openDirectoryFile);     // flush to disk
	}
    
    delete fileHdr;
	delete openDirectoryFile;
    delete directory;
    delete freeMap;
    return TRUE;
} 

//----------------------------------------------------------------------
// FileSystem::Write
//    "buffer": the pointer for written content.
//    "size": the size of written content.
//    "id": the identity of an opened file.
//    
//    return the number of content which has been written into file.
//----------------------------------------------------------------------

int
FileSystem::Write(char *buffer, int size, int id)
{
	OpenFile *openFile = NULL;
	
	if(id <= 0)
		return 0;
	
	openFile = (OpenFile*)(id);
	
	if(openFile == NULL)
		return 0;
	
	return openFile->Write(buffer, size);
}

//----------------------------------------------------------------------
// FileSystem::Read
//    "buffer": the pointer for read content.
//    "size": the size of read content.
//    "id": the identity of an opened file.
//    
//    return the number of content which has been read into buffer.
//----------------------------------------------------------------------
int
FileSystem::Read(char *buffer, int size, int id)
{
	OpenFile *openFile = NULL;
	
	if(id <= 0)
		return 0;
	
	openFile = (OpenFile*)(id);
	
	if(openFile == NULL)
		return 0;
	
	return openFile->Read(buffer, size);
}


//----------------------------------------------------------------------
// FileSystem::List
// 	List all the files in the file system directory.
//----------------------------------------------------------------------

void
FileSystem::List()
{
    Directory *directory = new Directory(NumDirEntries);

    directory->FetchFrom(directoryFile);
    directory->List();
    delete directory;
}

//----------------------------------------------------------------------
// FileSystem::Print
// 	Print everything about the file system:
//	  the contents of the bitmap
//	  the contents of the directory
//	  for each file in the directory,
//	      the contents of the file header
//	      the data in the file
//----------------------------------------------------------------------

void
FileSystem::Print()
{
    FileHeader *bitHdr = new FileHeader;
    FileHeader *dirHdr = new FileHeader;
    PersistentBitmap *freeMap = new PersistentBitmap(freeMapFile,NumSectors);
    Directory *directory = new Directory(NumDirEntries);

    printf("Bit map file header:\n");
    bitHdr->FetchFrom(FreeMapSector);
    bitHdr->Print();

    printf("Directory file header:\n");
    dirHdr->FetchFrom(DirectorySector);
    dirHdr->Print();

    freeMap->Print();

    directory->FetchFrom(directoryFile);
    directory->Print();

    delete bitHdr;
    delete dirHdr;
    delete freeMap;
    delete directory;
} 

//----------------------------------------------------------------------
// FileSystem::ListDirectory
// 	List all the files or subdirectories in the directory.
//----------------------------------------------------------------------

void
FileSystem::ListDirectory(char *path)
{
	char folder[10][10];
	char *cut = "/";
	char *pch;
	int sector, count = 0;
	bool success = TRUE;
	
    Directory *directory = new Directory(NumDirEntries);
	OpenFile *tempDirectory = new OpenFile(DirectorySector);
    directory->FetchFrom(directoryFile);
    
	pch = strtok(path, cut);
	
	while(pch != NULL) {
		strncpy(folder[count++], pch, FileNameMaxLen);
		pch = strtok(NULL, cut);
	}
	
	for(int i=0; i < count; i++) {
		if((sector = directory->Find(folder[i])) == -1) {
			success = FALSE;
			break;
		}
		
		if(tempDirectory != NULL) {
			delete tempDirectory;
			tempDirectory = NULL;
		}
		tempDirectory = new OpenFile(sector);
		directory->FetchFrom(tempDirectory);
	}
	
	if(success)
		directory->List();
	else 
		printf("No such directory\n");
	
	delete tempDirectory;
    delete directory;
}

//----------------------------------------------------------------------
// FileSystem::RecurListDirectory
// 	Recursively list all the files or subdirectories in the directory.
//----------------------------------------------------------------------

void
FileSystem::RecurListDirectory(char *path)
{
	Directory *directory;
	OpenFile *openDirectoryFile = NULL;
    int sector, count = 0;
	char folder[10][10];
    
    directory = new Directory(NumDirEntries);
	openDirectoryFile = Parse(path, FALSE, folder, &count);
	
	
	if(openDirectoryFile == NULL) {
		delete directory;
		printf("No such directory.\n");
		return;
	} else {
		directory->FetchFrom(openDirectoryFile);
		directory->RecurList(0);
	}
	delete directory;
	delete openDirectoryFile;
	return;
}

//----------------------------------------------------------------------
// FileSystem::RecurRemoveDirectory
// 	Recursively remove all the files or subdirectories in the directory.
//----------------------------------------------------------------------
bool
FileSystem::RecurRemoveDirectory(char *name) 	
{
	Directory *directory = new Directory(NumDirEntries);
	PersistentBitmap *freeMap;
    FileHeader *fileHdr;
	OpenFile *openDirectoryFile = NULL;
	OpenFile *openRemoveDirectory = NULL;
	int sector, count = 0;
	char folder[10][10];
	
	openDirectoryFile = Parse(name, TRUE, folder, &count);
	
	ASSERT(openDirectoryFile != NULL);

	directory->FetchFrom(openDirectoryFile);
	
	sector = directory->Find(folder[count - 1]);
	
	if(sector != -1) {
		openRemoveDirectory = new OpenFile(sector);
		directory->FetchFrom(openRemoveDirectory);
		
		fileHdr = new FileHeader;
		fileHdr->FetchFrom(sector);

		freeMap = new PersistentBitmap(freeMapFile,NumSectors);
	
		for(int i=0; i<NumDirEntries; i++) {
			if(directory->inUseIndex(i)) {
				char str[100];
				strcpy(str, name);
				strcat(str, "/");
				strcat(str, directory->getIndexName(i));
				
				if(!directory->isDirectory(i)) {
					Remove(str);
				} else {
					RecurRemoveDirectory(str);
				}
			}
		}
		fileHdr->Deallocate(freeMap);  		// remove data blocks
		freeMap->Clear(sector);			// remove header block
		
		directory->FetchFrom(openDirectoryFile);
		directory->Remove(folder[count-1]);
		
		freeMap->WriteBack(freeMapFile);
		directory->WriteBack(openDirectoryFile);
		
		delete openRemoveDirectory;
		delete fileHdr;
		delete freeMap;
					
	} else {
		printf("No such directory\n");
	}
	
	delete directory;
	delete openDirectoryFile;
}

//----------------------------------------------------------------------
// FileSystem::Parse
// 	Parse the path of input.
//----------------------------------------------------------------------'
OpenFile*
FileSystem::Parse(char *path, bool create, char folder[10][10], int *count)
{
	char pathCopy[100];
	char *cut = "/";
	char *pch;
	int sector;
	bool success = TRUE;
	
	strcpy(pathCopy, path);
	
    Directory *directory = new Directory(NumDirEntries);
	OpenFile *tempDirectory = new OpenFile(DirectorySector);
    directory->FetchFrom(directoryFile);
    
	pch = strtok(pathCopy, cut);
	
	while(pch != NULL) {
		strncpy(folder[(*count)++], pch, FileNameMaxLen);
		pch = strtok(NULL, cut);
	}
	
	for(int i=0; i < *count - (int)create; i++) {
		if((sector = directory->Find(folder[i])) == -1) {
			success = FALSE;
			delete tempDirectory;
			break;
		}
		
		if(tempDirectory != NULL) {
			delete tempDirectory;
			tempDirectory = NULL;
		}
		tempDirectory = new OpenFile(sector);
		directory->FetchFrom(tempDirectory);
	}
	
	delete directory;
	
	if(success)
		return tempDirectory;
	else 
		return NULL;
}

#endif // FILESYS_STUB
