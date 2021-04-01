#ifndef INDEX_H
#define INDEX_H

#include <stdint.h>
#include "context.h"
#include "safe-string.h"

#define FILE_ATTR_READONLY (0x00000001)
#define FILE_ATTR_HIDDEN (0x00000002)
#define FILE_ATTR_SYSTEM (0x00000004)
#define FILE_ATTR_DIRECTORY (0x00000010)						/* Note FILE_ATTR_DIRECTORY is not considered valid in NT.  It is  reserved
																   for the DOS SUBDIRECTORY flag. */
#define FILE_ATTR_ARCHIVE (0x00000020)
#define FILE_ATTR_DEVICE (0x00000040)
#define FILE_ATTR_NORMAL (0x00000080)
#define FILE_ATTR_TEMPORARY (0x00000100)
#define FILE_ATTR_SPARSE_FILE (0x00000200)
#define FILE_ATTR_REPARSE_POINT (0x00000400)
#define FILE_ATTR_COMPRESSED (0x00000800)
#define FILE_ATTR_OFFLINE (0x00001000)
#define FILE_ATTR_NOT_CONTENT_INDEXED (0x00002000)
#define FILE_ATTR_ENCRYPTED (0x00004000)
#define FILE_ATTR_VALID_FLAGS (0x00007fb7)						/* Note FILE_ATTR_VALID_FLAGS masks out the old DOS VolId and the
																   FILE_ATTR_DEVICE and preserves everything else.  This mask is
																   used to obtain all flags that are valid for reading. */
#define FILE_ATTR_VALID_SET_FLAGS (0x000031a7)					/* Note FILE_ATTR_VALID_SET_FLAGS masks out the old DOS VolId the
																   F_A_DEVICE F_A_DIRECTORY F_A_SPARSE_FILE F_A_REPARSE_POINT
																   F_A_COMPRESSED and F_A_ENCRYPTED and preserves the rest.  This mask
																   is used to obtain all flags that are valid for setting. */
//#define FILE_ATTRIBUTE_INTEGRITY_STREAM (0x00008000)
//#define FILE_ATTRIBUTE_VIRTUAL (0x00010000)
//#define FILE_ATTRIBUTE_NO_SCRUB_DATA (0x00020000)

#define FILE_ATTR_DUP_FILE_NAME_INDEX_PRESENT (0x10000000)	    /* Copy of the corresponding bit from the mft record telling us
																   whether this is a directory or not i.e. whether it has
																   an index root attribute or not.This flag is present in all
																   FILENAME_ATTR attributes but not in the STANDARD_INFORMATION
																   attribute of an mft record. */
#define FILE_ATTR_DUP_VIEW_INDEX_PRESENT (0x20000000)		    /* Copy of the corresponding bit from the mft record, telling us
																   whether this file has a view index present (eg. object id
																   index, quota index, one of the security indexes or the
																   encrypting filesystem related indexes).   */


#pragma pack (push, 1)
typedef struct _index_entry {
	/*0x00*/ uint64_t mft_reference;		/* MFT Reference of the file */
	/*0x08*/ uint16_t entry_size;           /* Size of this index entry */
	/*0x0a*/ uint16_t filename_offs;		/* Offset to the filename */
	/*0x0c*/ uint16_t index_flags;
	/*0x0e*/ uint8_t reserverd[2];
	/*0x10*/ uint64_t parent_mft_ref;		/* File Reference of the parent */
	/*0x18*/ uint64_t creation_tm;			/* 8 File creation time */
	/*0x20*/ uint64_t last_modif_tm;		/* Last modification time */
	/*0x28*/ uint64_t file_last_modif_tm;	/* Last modification time for FILE record*/
	/*0x30*/ uint64_t last_access_tm;		/* Last access time */
	/*0x38*/ uint64_t allocated_sz;			/* Allocated size of file */
	/*0x40*/ uint64_t real_sz;				/* Real size of file */
	/*0x48*/ uint64_t file_flags;
	/*0x50*/ uint8_t filename_len;			/* Length of filename */
	/*0x51*/ uint8_t namespace;				/* Filename namespace*/
	/*0x52*/ uint8_t filename[1];
	//There is actually some stuff after the filename, but it is not possible to
	//include this in the struct, since the filename is variable length
}* index_entry;
#pragma pack(pop)

#define IndexEntryPtr(buf) ((index_entry)(buf->buffer))

typedef struct {
	bytes original;
	bytes dereferenced;
} *resolved_index;

typedef struct _index_iter *index_iter;

index_iter StartIndexIterator(execution_context context, const index_entry root);

void CloseIndexIterator(index_iter iter);

index_entry CurrentIterEntry(const index_iter iter);

index_entry NextIterEntry(execution_context context, index_iter iter);

uint64_t SubNodeEntry(const index_entry rec);

bool FileFlagsFromIndexRec(const index_entry rec, string dest);

bytes FindIndexEntry(execution_context context, uint64_t parent_mft, const wchar_t* name);


#endif INDEX_H