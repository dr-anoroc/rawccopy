#ifndef ATTRIBS_H
#define ATTRIBS_H

#include "helpers.h"
#include "byte-buffer.h"
#include "fileio.h"

#define  ATTR_STANDARD_INFORMATION 0x00000010
#define  ATTR_ATTRIBUTE_LIST 0x00000020
#define  ATTR_FILE_NAME 0x00000030
#define  ATTR_OBJECT_ID 0x00000040
#define  ATTR_SECURITY_DESCRIPTOR 0x00000050
#define  ATTR_VOLUME_NAME 0x00000060
#define  ATTR_VOLUME_INFORMATION 0x00000070
#define  ATTR_DATA 0x00000080
#define  ATTR_INDEX_ROOT  0x00000090
#define  ATTR_INDEX_ALLOCATION  0x000000A0
#define  ATTR_BITMAP  0x000000B0
#define  ATTR_REPARSE_POINT  0x000000C0
#define  ATTR_EA_INFORMATION  0x000000D0
#define  ATTR_EA  0x000000E0
#define  ATTR_PROPERTY_SET  0x000000F0
#define  ATTR_LOGGED_UTILITY_STREAM  0x00000100
#define  ATTR_ATTRIBUTE_END_MARKER 0xFFFFFFFF

#define FILE_NAME_POSIX 0x00
#define	FILE_NAME_WIN32 0x01
#define	FILE_NAME_DOS 0x02
#define	FILE_NAME_WIN32_AND_DOS 0x03


#define AttrTypeToIndex(type) ((((uint32_t)type - 1) & 0x000000F0) >> 4)
#define IndexToAttrType(ind) (((uint32_t)(ind + 1))<<4)
#define AttrTypeFlag(type) (uint32_t)(1 << (AttrTypeToIndex(type)))

#pragma pack (push, 1)
struct _attribute {
	/*Ofs*/
	/*  0*/	uint32_t type;			// The (32-bit) type of the attribute. 
	/*  4*/	uint32_t length;		// Byte size of the resident part of the
									// attribute (aligned to 8-byte boundary).
									// Used to get to the next attribute.
	/*  8*/	uint8_t non_resident;	// If 0, attribute is resident.
									// If 1, attribute is non-resident.
	/*  9*/	uint8_t name_len;		// Unicode character size of name of attribute.
									// 0 if unnamed. 
	/* 10*/	uint16_t name_offs;		// If name_length != 0, the byte offset to the
									// beginning of the name from the attribute
									// record. Note that the name is stored as a
									// Unicode string. When creating, place offset
									// just at the end of the record header. Then,
									// follow with attribute value or mapping pairs
									// array, resident and non-resident attributes
									// respectively, aligning to an 8-byte boundary.
	/* 12*/	uint16_t flags;			// Flags describing the attribute.
	/* 14*/	uint16_t attrib_id;		// The instance of this attribute record. This
									// number is unique within this mft record
	/* 16*/	union {
		// Resident attributes.
				struct {
					/* 16 */	uint32_t value_len;		// Byte size of attribute value.
					/* 20 */	uint16_t value_offs;	// Byte offset of the attribute
														// value from the start of the
														// attribute record.
					/* 22 */	uint8_t resid_fags;  // Specific flags for resident attribute
					/* 23 */	uint8_t reserved0;
									 
				};
		// Non-resident attributes.
		struct {
			/* 16*/	uint64_t start_vcn;					// Lowest valid virtual cluster number
														// for this portion of the attribute value or
														// 0 if this is the only extent (usually the
														// case). - Only when an attribute list is used
														// does lowest_vcn != 0 ever occur. 
			/* 24*/	uint64_t end_vcn;					// Highest valid vcn of this extent of
														// the attribute value. - Usually there is only one
														// portion, so this usually equals the attribute
														// value size in clusters minus 1. Can be -1 for
														// zero length files. Can be 0 for "single extent"
														// attributes.
			/* 32*/ uint16_t run_list_offs;				// Byte offset from the beginning of the structure to
														// the mapping pairs array which contains the mappings between
														// the vcns and the logical cluster numbers (lcns).
			/* 34*/	uint8_t compr_unit;					// The compression unit expressed
														// as the log to the base 2 of the number of
														// clusters in a compression unit.  0 means not
														// compressed.  (This effectively limits the
														// compression unit size to be a power of two
														// clusters.)  WinNT4 only uses a value of 4.
														// Sparse files have this set to 0 on XPSP2. */
			/* 35*/	uint8_t reserved1[5];	
			/* 40*/	uint64_t alloc_sz;					// Byte size of disk space allocated to hold the attribute value.
														// Always is a multiple of the cluster size. When a file
														// is compressed, this field is a multiple of the
														// compression block size (2^compression_unit) and
														// it represents the logically allocated space
														// rather than the actual on disk usage. For this
														// use the compressed_size (see below).
			/* 48*/	uint64_t real_sz;					// Byte size of the attribute value. Can be larger than
														// AllocatedSize if attribute value is compressed or sparse.
			/* 56*/ uint64_t init_sz;					// Byte size of initialized	portion of the attribute value.
														// Usually equals RealSize.
			/* 64*/ uint64_t compr_sz;					// Byte size of the attribute value after compression.
														// Only present when compressed or sparse.  Always is a multiple
														// of the cluster size.  Represents the actual amount
														// of disk space being used on the disk.
		};
	};
};
#pragma pack(pop)

typedef struct _attribute* attribute;

typedef struct _file_name_attribute* file_name_attribute;



#define ATTR_IS_COMPRESSED 0x0001
#define ATTR_IS_SPARSE 0x8000

wchar_t* AttributeTypeName(uint32_t type);

attribute AttributePtr(bytes buf, size_t offset);

wchar_t* GetAttributeName(attribute attrib);

bytes GetAttributeData(cluster_reader cr, attribute attrib);

UT_array* GetAttributeListRecords(cluster_reader cr, uint64_t file_ref, const attribute attrib);

wchar_t* NameFromNameAttr(attribute name_attr);

uint8_t NameSpaceFromNameAttr(attribute name_attr);

const wchar_t* NameSpaceLabel(uint8_t ns);

#endif ATTRIBS_H
