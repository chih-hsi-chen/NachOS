// filehdr.cc 
//	Routines for managing the disk file header (in UNIX, this
//	would be called the i-node).
//
//	The file header is used to locate where on disk the 
//	file's data is stored.  We implement this as a fixed size
//	table of pointers -- each entry in the table points to the 
//	disk sector containing that portion of the file data
//	(in other words, there are no indirect or doubly indirect 
//	blocks). The table size is chosen so that the file header
//	will be just big enough to fit in one disk sector, 
//
//      Unlike in a real system, we do not keep track of file permissions, 
//	ownership, last modification date, etc., in the file header. 
//
//	A file header can be initialized in two ways:
//	   for a new file, by modifying the in-memory data structure
//	     to point to the newly allocated data blocks
//	   for a file already on disk, by reading the file header from disk
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"

#include "filehdr.h"
#include "debug.h"
#include "synchdisk.h"
#include "main.h"

//----------------------------------------------------------------------
// MP4 mod tag
// FileHeader::FileHeader
//	There is no need to initialize a fileheader,
//	since all the information should be initialized by Allocate or FetchFrom.
//	The purpose of this function is to keep valgrind happy.
//----------------------------------------------------------------------
FileHeader::FileHeader()
{
	numBytes = 0;
	numSectors = 0;
	memset(dataSectors, -1, sizeof(dataSectors));
	
	// MP4
	headerSector = -1;
	singleIndirectSector = -1;
	doubleIndirectSector = -1;
}

//----------------------------------------------------------------------
// MP4 mod tag
// FileHeader::~FileHeader
//	Currently, there is not need to do anything in destructor function.
//	However, if you decide to add some "in-core" data in header
//	Always remember to deallocate their space or you will leak memory
//----------------------------------------------------------------------
FileHeader::~FileHeader()
{
	// nothing to do now
}

//----------------------------------------------------------------------
// MP4 mod tag
// FileHeader::Initialize
// Initialize the content of file header
//----------------------------------------------------------------------
void 
FileHeader::Initialize()
{
	
}

//----------------------------------------------------------------------
// FileHeader::Allocate
// 	Initialize a fresh file header for a newly created file.
//	Allocate data blocks for the file out of the map of free disk blocks.
//	Return FALSE if there are not enough free blocks to accomodate
//	the new file.
//
//	"freeMap" is the bit map of free disk sectors
//	"fileSize" is the bit map of free disk sectors
//----------------------------------------------------------------------

bool
FileHeader::Allocate(PersistentBitmap *freeMap, int fileSize)
{ 
    // numBytes = fileSize;
    // numSectors  = divRoundUp(fileSize, SectorSize);
    if (freeMap->NumClear() < numSectors) {
		cout << "OUT OF MEMORY\n";
		return FALSE;		// not enough space
	}
		
	AllocateDirectBlocks(fileSize, freeMap);
	if(fileSize <= (NumDirect * SectorSize)) return TRUE;
	
	// Create Single Indirect
	ASSERT(numBytes >= (NumDirect * SectorSize));
	CreateSingleIndirectBlock(fileSize, freeMap);
	
	int allocated = AllocateIndirectSpace(fileSize, singleIndirectSector, (NumDirect * SectorSize), freeMap);
	if(allocated == 0) return TRUE;
	
	// Create Double Indirect
	ASSERT(allocated > 0);
	AllocateDoubleIndirectBlock(fileSize, freeMap);
	
	
    return TRUE;
}

//----------------------------------------------------------------------
// FileHeader::Deallocate
// 	De-allocate all the space allocated for data blocks for this file.
//
//	"freeMap" is the bit map of free disk sectors
//----------------------------------------------------------------------

void 
FileHeader::Deallocate(PersistentBitmap *freeMap)
{
    /*for (int i = 0; i < numSectors; i++) {
		ASSERT(freeMap->Test((int) dataSectors[i]));  // ought to be marked!
		freeMap->Clear((int) dataSectors[i]);
    }*/
	
	for (int i = 0; i < numSectors; i++) {
      int pos = GetPhysicSector(i);
      ASSERT(freeMap->Test(pos));  // ought to be marked!
      freeMap->Clear(pos);
    }
    if (singleIndirectSector != -1) { 
      if (freeMap->Test(singleIndirectSector)) freeMap->Clear(singleIndirectSector);
    }

    if (doubleIndirectSector != -1) {
      if (freeMap->Test(doubleIndirectSector)) freeMap->Clear(doubleIndirectSector);
    }
}

//----------------------------------------------------------------------
// FileHeader::FetchFrom
// 	Fetch contents of file header from disk. 
//
//	"sector" is the disk sector containing the file header
//----------------------------------------------------------------------

void
FileHeader::FetchFrom(int sector)
{
    kernel->synchDisk->ReadSector(sector, (char *)this);
	
	/*
		MP4 Hint:
		After you add some in-core informations, you will need to rebuild the header's structure
	*/
	
}

//----------------------------------------------------------------------
// FileHeader::WriteBack
// 	Write the modified contents of the file header back to disk. 
//
//	"sector" is the disk sector to contain the file header
//----------------------------------------------------------------------

void
FileHeader::WriteBack(int sector)
{
    kernel->synchDisk->WriteSector(sector, (char *)this); 
	
	/*
		MP4 Hint:
		After you add some in-core informations, you may not want to write all fields into disk.
		Use this instead:
		char buf[SectorSize];
		memcpy(buf + offset, &dataToBeWritten, sizeof(dataToBeWritten));
		...
	*/
	
}

//----------------------------------------------------------------------
// FileHeader::ByteToSector
// 	Return which disk sector is storing a particular byte within the file.
//      This is essentially a translation from a virtual address (the
//	offset in the file) to a physical address (the sector where the
//	data at the offset is stored).
//
//	"offset" is the location within the file of the byte in question
//----------------------------------------------------------------------

int
FileHeader::ByteToSector(int offset)
{
	int localSector = offset / SectorSize;
	
    return GetPhysicSector(localSector);
}

//----------------------------------------------------------------------
// FileHeader::FileLength
// 	Return the number of bytes in the file.
//----------------------------------------------------------------------

int
FileHeader::FileLength()
{
    return numBytes;
}

//----------------------------------------------------------------------
// FileHeader::Print
// 	Print the contents of the file header, and the contents of all
//	the data blocks pointed to by the file header.
//----------------------------------------------------------------------

void
FileHeader::Print()
{
    int i, j, k;
    char *data = new char[SectorSize];

    printf("FileHeader contents.  File size: %d.  File blocks:\n", numBytes);
    for (i = 0; i < numSectors; i++) {
		int sector = GetPhysicSector(i);
		
		if(i < NumDirect)
			printf("%d ", sector);
		else if(i < (NumDirect + NumIndirect))
			printf("*%d* ", sector);
		else
			printf("**%d** ", sector);
	}

    printf("\nFile contents:\n");
	
    for (i = k = 0; i < numSectors; i++) {
		kernel->synchDisk->ReadSector(GetPhysicSector(i), data);
		
        for (j = 0; (j < SectorSize) && (k < numBytes); j++, k++) {
			if ('\040' <= data[j] && data[j] <= '\176')   // isprint(data[j])
			printf("%c", data[j]);
				else
			printf("\\%x", (unsigned char)data[j]);
		}
        printf("\n"); 
    }
    delete [] data;
}

// Allocates space within the direct blocks of the inode.
void
FileHeader::AllocateDirectBlocks(int fileSize, PersistentBitmap *freeMap)
{
  while (numBytes >= 0 && numBytes < (NumDirect * SectorSize)) {
    if (fileSize <= (numSectors * SectorSize)) {
		numBytes = fileSize;
		break;
    } else {
      numBytes = (numSectors * SectorSize);
	
      if (numSectors < NumDirect) {
		// This action needs to be atomic
		dataSectors[numSectors] = freeMap->FindAndSet();
		DEBUG(dbgFile, "Adding sector " << dataSectors[numSectors] << " to the direct block\n");
		numSectors += 1;
      }
    }
  }
}

// Create Single Indirect Block for file header
void 
FileHeader::CreateSingleIndirectBlock(int fileSize, PersistentBitmap *freeMap)
{
	if(singleIndirectSector == -1) {
		Indirect *singleIndirect = new Indirect();
		
		singleIndirectSector = freeMap->FindAndSet();
		DEBUG(dbgFile, "Creating Single Indirect Block at sector " << singleIndirectSector << "\n");
		//singleIndirect->numSectors = 0;
		kernel->synchDisk->WriteSector(singleIndirectSector, (char *) singleIndirect);
		
		delete singleIndirect;
	}
}

// Allocate Space for Indirect Block
int
FileHeader::AllocateIndirectSpace(int fileSize, int sector, int start, PersistentBitmap *freeMap)
{
	int end = start + (NumIndirect * SectorSize);
	Indirect *indirect = new Indirect();
	
	kernel->synchDisk->ReadSector(sector, (char *)indirect);
	
	while(numBytes >= start && numBytes < end) {
		if(fileSize <= (numSectors * SectorSize)) {
			numBytes = fileSize;
			break;
		} else {
			numBytes = (numSectors * SectorSize);
			
			if(indirect->numSectors < NumIndirect) {
				indirect->dataSectors[indirect->numSectors] = freeMap->FindAndSet();
				
				indirect->numSectors++;
				numSectors++;
			}
		}
	}
	kernel->synchDisk->WriteSector(sector, (char *)indirect);
	delete indirect;
	
	return fileSize - numBytes;
}

// Allocate Double Indirect Block
void 
FileHeader::AllocateDoubleIndirectBlock(int fileSize, PersistentBitmap *freeMap)
{
	int allocated = -1;
	
	Indirect *doubleIndirect = new Indirect();
	
	if(doubleIndirectSector != -1) {
		kernel->synchDisk->ReadSector(doubleIndirectSector, (char *)doubleIndirect);
	} else {
		doubleIndirectSector = freeMap->FindAndSet();
		
		
		DEBUG(dbgFile, "Creating a double indirect block at sector " << doubleIndirectSector << "\n");
		
		kernel->synchDisk->WriteSector(doubleIndirectSector, (char *)doubleIndirect);
	}
	
	int cur_numSectors = 0;
	
	do{
		ASSERT(cur_numSectors <= doubleIndirect->numSectors);
		Indirect *singleIndirect = new Indirect();
		
		if(doubleIndirect->dataSectors[cur_numSectors] != -1) {
			kernel->synchDisk->ReadSector(doubleIndirect->dataSectors[cur_numSectors], (char *)singleIndirect);
		} else {
			int indSector = freeMap->FindAndSet();
			
			DEBUG(dbgFile, "Creating a single indirect of double indirect at sector " << indSector << "\n");
			
			//singleIndirect->numSectors = 0;
			doubleIndirect->dataSectors[doubleIndirect->numSectors] = indSector;
			doubleIndirect->numSectors++;
			
			kernel->synchDisk->WriteSector(indSector, (char *)singleIndirect);
			kernel->synchDisk->WriteSector(doubleIndirectSector, (char *) doubleIndirect);
		}
		
		// start position = Direct_Sector + Single_Sector + SingleSector_OF_Double
		int start = SectorSize * (NumDirect + NumIndirect * (1 + cur_numSectors));
		
		allocated = AllocateIndirectSpace(fileSize, doubleIndirect->dataSectors[cur_numSectors], start, freeMap);
		
		// Whether current single indirect block 
		// can be filled completely or not just increment the index
		cur_numSectors++;
		
		delete singleIndirect;
		
	}while(allocated != 0);
	
	delete doubleIndirect;
}

// Return the physical sector number
int
FileHeader::GetPhysicSector(int localSector)
{
	int physicSector;
	
	if(localSector < NumDirect) {
		physicSector =  dataSectors[localSector];
	} else if(localSector < (NumDirect + NumIndirect)) {
		ASSERT(singleIndirectSector != -1);
		ASSERT(localSector < (NumDirect + NumIndirect));
		
		Indirect *singleIndirect = new Indirect();
		
		kernel->synchDisk->ReadSector(singleIndirectSector, (char *)singleIndirect);
		physicSector = singleIndirect->dataSectors[localSector - NumDirect];
		
		delete singleIndirect;
		
	} else {
		ASSERT(doubleIndirectSector != -1);
		ASSERT(localSector >= (NumDirect + NumIndirect));
		
		Indirect *doubleIndirect = new Indirect();
		kernel->synchDisk->ReadSector(doubleIndirectSector, (char *)doubleIndirect);
		
		int single = (localSector - (NumDirect + NumIndirect)) / NumIndirect;
		
		Indirect *ind = new Indirect();
		kernel->synchDisk->ReadSector(doubleIndirect->dataSectors[single], (char *)ind);
		
		int pos = (localSector - (NumDirect + NumIndirect)) % NumIndirect;
		
		physicSector = ind->dataSectors[pos];
		
		delete doubleIndirect;
		delete ind;
	}
	return physicSector;
}






