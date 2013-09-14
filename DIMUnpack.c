#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <memory.h>

#if defined(WIN32) || defined(__WINDOWS__)
#include <direct.h>	// for _mkdir()
#else
#include <sys/stat.h>
#define _mkdir(dir)	mkdir(x, 0777)
#endif

#include "stdtype.h"

#if defined(_MSC_VER)
#define INLINE	static __inline
#elif defined(__GNUC__)
#define INLINE	static __inline__
#else
#define INLINE	static inline
#endif


#pragma pack(1)
typedef struct _fat_boot_sector
{
	UINT16 BytPerSect;
	UINT8 SectPerCluster;
	UINT16 ReservedSect;
	UINT8 NumFats;
	UINT16 RootDirEntries;
	UINT16 LogicalSect;
	UINT8 MediumDesc;
	UINT16 SectPerFat;
	UINT16 SectPerTrk;
	UINT16 Heads;
	UINT16 HiddenSect;
	UINT8 Reserved[13];
	char DiskName[11];
	char FileSysType[8];
} FAT_BOOTSECT;

typedef struct _fat_file_entry
{
	char Name[8];
	char Extension[3];
	UINT8 Attribute;
	UINT8 Reserved[10];
	UINT16 Time;
	UINT16 Date;
	UINT16 StartCluster;
	UINT32 FileLength;
} FAT_ENTRY;
#pragma pack()


UINT32 DimSize;
UINT8* DimData;
FAT_BOOTSECT BootSect;
UINT32 ClusterBase;
UINT16 ClusterSize;
UINT16 FATEntries;
UINT16* FATTbl;

INLINE UINT16 ReadLE16(const UINT8* Data);
INLINE UINT16 ReadBE16(const UINT8* Data);
static void ReadFAT(UINT32 BasePos);
static void ReadDirectory(UINT32 Cluster, UINT16 NumEntries, const char* BasePath, UINT8 Layer);
static void BuildFilename(char* DestBuf, FAT_ENTRY* Entry);
static void ExtractFile(const char* FileName, UINT16 Cluster, UINT32 FileSize);

int main(int argc, char* argv[])
{
	FILE* hFile;
	char BootSig[0x11];
	UINT8 BootFmt;
	UINT8 BaseSects;
	UINT16 RootDirSize;
	UINT32 StartPos;
	char* OutPath;
	
	if (argc <= 2)
	{
		printf("Usage: DIMUnpack.exe DiskImg.dim OutPath\\\n");
		return 0;
	}
	
	hFile = fopen(argv[1], "rb");
	if (hFile == NULL)
		return 1;
	
	fseek(hFile, 0x00, SEEK_END);
	DimSize = ftell(hFile);
	
	fseek(hFile, 0x00, SEEK_SET);
	DimData = (UINT8*)malloc(DimSize);
	fread(DimData, 0x01, DimSize, hFile);
	
	fclose(hFile);
	
	strncpy(BootSig, (char*)&DimData[0x102], 0x10);
	BootSig[0x10] = '\0';
	printf("Disk Format:\t%s\n", BootSig);
	
	if (BootSig[0] == '\x90')
	{
		// Verified to work with:
		//	"\x90X68IPL30"	Asuka 120 Percent Burning Fest
		//	"\x90NEC 2.00"	Arcus Odyssey [Disk 2]
		BootFmt = 0x01;
		StartPos = 0x10B;
		BootSect.BytPerSect =	ReadLE16(	&DimData[StartPos + 0x00]);
		BootSect.SectPerCluster =			 DimData[StartPos + 0x02];
		BootSect.ReservedSect =	ReadLE16(	&DimData[StartPos + 0x03]);
		BootSect.NumFats =					 DimData[StartPos + 0x05];
		BootSect.RootDirEntries = ReadLE16(	&DimData[StartPos + 0x06]);
		BootSect.LogicalSect =	ReadLE16(	&DimData[StartPos + 0x08]);
		BootSect.MediumDesc =				 DimData[StartPos + 0x0A];
		BootSect.SectPerFat =	ReadLE16(	&DimData[StartPos + 0x0B]);
		BootSect.SectPerTrk =	ReadLE16(	&DimData[StartPos + 0x0D]);
		BootSect.Heads =		ReadLE16(	&DimData[StartPos + 0x0F]);
		BootSect.HiddenSect =	ReadLE16(	&DimData[StartPos + 0x11]);
		memcpy(BootSect.Reserved,			&DimData[StartPos + 0x13], 0x0D);
		memcpy(BootSect.DiskName,			&DimData[StartPos + 0x20], 0x0B);
		memcpy(BootSect.FileSysType,		&DimData[StartPos + 0x2B], 0x08);
	}
	else if (! strncmp(BootSig, "Hudson soft", 11))
	{
		BootFmt = 0x02;
		StartPos = 0x112;
		BootSect.BytPerSect =	ReadBE16(	&DimData[StartPos + 0x00]);
		BootSect.SectPerCluster =			 DimData[StartPos + 0x02];
		BootSect.NumFats =					 DimData[StartPos + 0x03];
		BootSect.ReservedSect =	ReadBE16(	&DimData[StartPos + 0x04]);
		BootSect.RootDirEntries = ReadBE16(	&DimData[StartPos + 0x06]);
		BootSect.LogicalSect =	ReadBE16(	&DimData[StartPos + 0x08]);
		BootSect.MediumDesc =				 DimData[StartPos + 0x0A];
		BootSect.SectPerFat =				 DimData[StartPos + 0x0B];
		BootSect.SectPerTrk =	0;
		BootSect.Heads =		0;
		BootSect.HiddenSect =	0;
		memset(BootSect.Reserved,		0x00, 0x0D);
		memset(BootSect.DiskName,		0x00, 0x0B);
		memset(BootSect.FileSysType,	0x00, 0x08);
	}
	else
	{
		printf("Unknown disk format!\n");
		free(DimData);
		return 2;
	}
	printf("Bytes per Sector:\t%hu\n", BootSect.BytPerSect);		BootSect.BytPerSect=1024;
	printf("Boot Sectors:\t\t%hu\n", BootSect.ReservedSect);		BootSect.ReservedSect=1;
	printf("RootDir Entries:\t%hu\n", BootSect.RootDirEntries);		BootSect.RootDirEntries=192;
	printf("Sectors per Cluster:\t%hu\n", BootSect.SectPerCluster);	BootSect.SectPerCluster=1;
	
	BootSect.NumFats = 2; BootSect.SectPerFat = 2;
	BaseSects = BootSect.ReservedSect + BootSect.NumFats * BootSect.SectPerFat;
	RootDirSize = BootSect.RootDirEntries * 32;
	RootDirSize = ((RootDirSize - 1) | (BootSect.BytPerSect - 1)) + 1;	// round up to full sectors
	
	ClusterSize = BootSect.SectPerCluster * BootSect.BytPerSect;
	ClusterBase = 0x100 + BaseSects * BootSect.BytPerSect + RootDirSize - 2 * ClusterSize;	// the first Cluster has number 2
	printf("Cluster Base:\t0x%04X\n", ClusterBase);
	printf("\n");
	
	ReadFAT(0x100 + BootSect.ReservedSect * BootSect.BytPerSect);
	
	OutPath = (char*)malloc(strlen(argv[2]) + 2);
	strcpy(OutPath, argv[2]);
	StartPos = strlen(OutPath);
	if (StartPos && (OutPath[StartPos - 1] == '\\' || OutPath[StartPos - 1] == '/'))
		OutPath[StartPos - 1] = '\0';
	_mkdir(OutPath);
	strcat(OutPath, "\\");
	_getch();
	ReadDirectory(0x100 + BaseSects * BootSect.BytPerSect, BootSect.RootDirEntries, OutPath, 0);
	
	free(OutPath);
	free(FATTbl);
	free(DimData);
	_getch();
	
	return 0;
}

INLINE UINT16 ReadLE16(const UINT8* Data)
{
	// read 16-Bit Word (Little Endian/Intel Byte Order)
	return (Data[0x01] << 8) | (Data[0x00] << 0);
}

INLINE UINT16 ReadBE16(const UINT8* Data)
{
	// read 16-Bit Word (Big Endian/Motorola Byte Order)
	return (Data[0x00] << 8) | (Data[0x01] << 0);
}

static void ReadFAT(UINT32 BasePos)
{
	UINT32 CurPos;
	UINT16 CurEnt;
	
	FATEntries = BootSect.SectPerFat * BootSect.BytPerSect / 3 * 2;	// 12 bit per FAT entry (*8/12)
	FATTbl = (UINT16*)malloc(sizeof(UINT16) * FATEntries);
	for (CurEnt = 0x00, CurPos = BasePos; CurEnt < FATEntries; CurEnt += 2, CurPos += 0x03)
	{
		FATTbl[CurEnt + 0] =	( DimData[CurPos + 0]         << 0) |
								((DimData[CurPos + 1] & 0x0F) << 8);
		FATTbl[CurEnt + 1] =	((DimData[CurPos + 1] & 0xF0) >> 4) |
								( DimData[CurPos + 2]         << 4);
	}
	
	return;
}

static void ReadDirectory(UINT32 Cluster, UINT16 NumEntries, const char* BasePath, UINT8 Layer)
{
	UINT32 DirBasePos;
	UINT16 CurClst;
	UINT32 CurPos;
	UINT32 EndPos;
	FAT_ENTRY* CurEntry;
	char* FileName;
	char* FileTitle;
	UINT8 TempByt;
	
	FileName = (char*)malloc(strlen(BasePath) + 16);	// Base + "8.3" + '\0' -> Base+13
	strcpy(FileName, BasePath);
	FileTitle = FileName + strlen(FileName);
	
	if (NumEntries)
	{
		// Root Directory, Cluster == Base Offset
		CurClst = 0x00;
		DirBasePos = Cluster;
		EndPos = DirBasePos + NumEntries * 0x20;
	}
	else
	{
		CurClst = (UINT16)Cluster;
	}
	
	do
	{
		if (CurClst)	// if NOT Root Directory
		{
			// another Directory
			DirBasePos = ClusterBase + ClusterSize * CurClst;
			EndPos = DirBasePos + ClusterSize;
		}
		
		for (CurPos = DirBasePos; CurPos < EndPos; CurPos += 0x20)
		{
			CurEntry = (FAT_ENTRY*)&DimData[CurPos];
			if (CurEntry->Name[0] == 0x00)
			{
				CurClst = 0x00;	// terminate instantly
				break;
			}
			
			BuildFilename(FileTitle, CurEntry);
			
			TempByt = Layer;
			while(TempByt --)
				putchar('\t');
			
			if ((CurEntry->Attribute & 0x10))	// Is Directory?
			{
				// "." and ".." are for changing directories
				if (strcmp(FileTitle, ".") && strcmp(FileTitle, ".."))
				{
					_mkdir(FileName);
					strcat(FileTitle, "\\");
					printf("%s\n", FileTitle);
					ReadDirectory(CurEntry->StartCluster, 0x00, FileName, Layer + 1);
				}
				else
				{
					printf("%s\n", FileTitle);
				}
			}
			else
			{
				printf("%s\n", FileTitle);
				ExtractFile(FileName, CurEntry->StartCluster, CurEntry->FileLength);
			}
		}
		CurClst = FATTbl[CurClst];
	} while(CurClst && CurClst < 0xFF0);
	// Cluster 0x000 is the "free" cluster, so I use it as terminator.
	// Clusters 0xFF0 are EOF or invalid clusters, so I stop here, too.
	
	return;
}

static void BuildFilename(char* DestBuf, FAT_ENTRY* Entry)
{
	UINT8 NameLen;
	UINT8 ExtLen;
	
	NameLen = 8;
	while(NameLen && Entry->Name[NameLen - 1] == ' ')
		NameLen --;
	
	ExtLen = 3;
	while(ExtLen && Entry->Extension[ExtLen - 1] == ' ')
		ExtLen --;
	
	if (ExtLen)
		sprintf(DestBuf, "%.*s.%.*s", NameLen, Entry->Name, ExtLen, Entry->Extension);
	else
		sprintf(DestBuf, "%.*s", NameLen, Entry->Name);
	
	return;
}

static void ExtractFile(const char* FileName, UINT16 Cluster, UINT32 FileSize)
{
	FILE* hFile;
	UINT16 CurClst;
	UINT8* Buffer;
	UINT32 ClstPos;
	UINT32 WrtBytes;
	
	hFile = fopen(FileName, "wb");
	if (hFile == NULL)
		return;
	
	Buffer = (UINT8*)malloc(ClusterSize);
	CurClst = Cluster;
	while(FileSize)
	{
		if (CurClst >= 0xFF0)
			break;
		WrtBytes = (FileSize > ClusterSize) ? ClusterSize : FileSize;
		ClstPos = ClusterBase + CurClst * ClusterSize;
		fwrite(&DimData[ClstPos], 0x01, WrtBytes, hFile);
		
		FileSize -= WrtBytes;
		CurClst = FATTbl[CurClst];
	}
	
	fclose(hFile);
	
	return;
}
