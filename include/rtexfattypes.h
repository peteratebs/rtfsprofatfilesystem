#ifndef __RTFSEXFATTYPES__
#define __RTFSEXFATTYPES__ 1


#define EXFATDIRENTSIZE 32
#define EXFATCHARSPERFILENAMEDIRENT 15

#define DEBUG_EXFAT_VERBOSE 0					/* Set to one to turn on diagnostics using rtfs console IO. */
#define DEBUG_EXFAT_PROBE_ROOT 0				/* Set to one to dump root directory info (requires printf) */



#define EXFAT_DIRENTTYPE_EOF				0x00
#define EXFAT_DIRENTTYPE_ALLOCATION_BITMAP  0x81
#define EXFAT_DIRENTTYPE_UPCASE_TABLE 		0x82
#define EXFAT_DIRENTTYPE_VOLUME_LABEL 		0x83
#define EXFAT_DIRENTTYPE_FILE 				0x85
#define EXFAT_DIRENTTYPE_GUID 				0xA0
#define EXFAT_DIRENTTYPE_STREAM_EXTENSION	0xC0
#define EXFAT_DIRENTTYPE_FILE_NAME_ENTRY	0xC1
#define EXFAT_DIRENTTYPE_VENDOR_EXTENSION	0xE0
#define EXFAT_DIRENTTYPE_VENDOR_ALLOCATION	0xE1

#define EXFAT_DIRENTTYPE_ABM 0x81 /* Allocation bit map */

#define EXFATDIRENTFIRSTCLOFFSET 20
#define EXFATDIRENTLENGTHOFFSET  24

#define EXFATALLOCATIONPOSSIBLE 0x1
#define EXFATNOFATCHAIN 		0x2

#define EXFATEOFCLUSTER 0xffffffff
#define EXFATBADCLUSTER 0xfffffff7

#define STANDARD_UCTABLE_SIZE 5836 /* bytes */
#define STANDARD_UCTABLE_CHECKSUM 0xe619d30d
extern const word cStandarducTableCompressed[STANDARD_UCTABLE_SIZE/2];
extern const word cStandarducTableunCompressed[65536];

/* HEREHERE Ifdefs around "hacks" to expedite getting a working prototype */

/* For now always extend directories, need to study microsoft's scheme they seem to extend only on the end, maybe they recycle at end of cluster. */
/* Commented out, means we reclaim directory entries as needed. */
/* #define EXFAT_EXTEND_DIRECTORIES_FROM_END_ONLY */


typedef struct exfatfileentry {
		byte 			EntryType;
		byte 			SecondaryCount;
		word 			SetChecksum;
		word 			FileAttributes;
		byte 			Reserved1[2];
		DATESTR 		CreateTimeStamp;
		DATESTR 		LastModifiedTimeStamp;
		DATESTR 		LastAccessedTimeStamp;
		byte		   	Create10msIncrement;
		byte		  	LastModified10msIncrement;
		byte		   	CreateUtcOffset;
		byte		   	LastModifiedUtcOffset;
		byte		   	LastAccessedUtcOffset;
		byte 		   	Reserved2[7];
} EXFATFILEENTRY;

typedef struct exfatstreamextensionentry {
		byte 			EntryType;
		byte 			GeneralSecondaryFlags;
		byte 			Reserved1;
		byte  			NameLen;
		word 			NameHash;
		byte 			Reserved2[2];
		ddword 			ValidDataLength;
		byte 			Reserved3[4];
		dword 			FirstCluster;
		ddword 			DataLength;
} EXFATSTREAMEXTENSIONENTRY;

typedef struct exfatfilenameentry {
		byte 			EntryType;
		byte 			GeneralSecondaryFlags;
		word 			FileName[15];
} EXFATFILENAMEENTRY;


typedef struct exfatdirscancontrol {
		/* Control elements for scanning */
		byte  expected_entry_type;
		byte  NameLen;
		int   NameLenProcessed;
		word  CheckSumExpected;
		byte  secondary_entries_expected;
		byte  secondary_entries_found;
		dword spans_sectors[3];
		int   first_index;
} EXFATDIRSCANCONTROL;


typedef struct exfatdirscan {
		byte rawfileentry[32];
		byte rawstreamextensionentry[32];
		byte rawfilenamedata[512];
		/* Control elements for scanning */
		EXFATDIRSCANCONTROL	control;
} EXFATDIRSCAN;


typedef struct exfatdateext {
	byte Create10msIncrement;
	byte LastModified10msIncrement;
	byte CreateUtcOffset;
	byte LastModifiedUtcOffset;
	byte LastAccessedUtcOffset;
} EXFATDATEEXT;

/* See pc_allocate_exfat_buffers() and callback code */
typedef struct exfatmountparms {
		/* Passed from Rtfs to the callback handler for RTFS_CBS_GETEXFATBUFFERS */
		int	  driveID;					/* 0-25 == A:-Z: */
		void  *pdr;					    /* Caste to acceess rtf drive structure directly */
		dword SectorSizeBytes;
		/* Bitmap info passed */
		dword BitMapSizeSectors;		/* Optimal size for bitmap */
		/* UpCase Table Info */
		dword UpcaseSizeBytes;		   /* Size for upcase table */

		/* Bitmap Caching stuff returned here */
		dword BitMapBufferSizeSectors;	/* Returned space for buffering */
		int   BitMapBufferPageSizeSectors;
		void  *BitMapBufferCore;
		void  *BitMapBufferControlCore;

		/* UpCase Table stuff returned note must return at least minimum. */
		void  *UpCaseBufferCore;

} EXFATMOUNTPARMS;


typedef struct exfatfileparseobj {
		BLKBUFF 		*pUpCasedFileNameBuffer;
		BLKBUFF 		*pUnicodeFileNameBuffer;
		word 			*upCasedLfn;
		word	 		*UnicodeLfn;
		int				use_charset;
		byte 			*pInputFile;
		int   			NameSegments;
		byte  			NameLen;
		word 			NameHash;
		/* If we are seeking to add to the entry */
		int 			segmentsRequired;		   /* Find this many free segments please */
		SEGDESC			Freesegments;			   /* Here is the answer */
} EXFATFILEPARSEOBJ;


#endif /*  __RTFSEXFATTYPES__ */
