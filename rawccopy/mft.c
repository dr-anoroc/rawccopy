#include "mft.h"
#include "bootsector.h"
#include "attribs.h"
#include "datarun.h"

const unsigned char RecordSignature[] = { 0x46, 0x49, 0x4C, 0x45 }; //FILE signature, is actually "FILE"
const unsigned char RecordSignatureBad[] = { 0x44, 0x41, 0x41, 0x42 }; // BAAD signature


typedef struct _mft_record
{
	UT_array* attributes[16];
} *mft_record;

#define MFT_RECORD_IN_USE 0x0001
#define MFT_RECORD_IS_DIRECTORY 0x0002

#pragma pack (push, 1)
struct _ntfs_record {
	/*  0*/ uint8_t magic[4];			// A four-byte magic identifying the record, contains strings like "FILE" or "INDX"
	/*  4*/ uint16_t usa_offs;			// Offset to the Update Sequence Array from the start of the ntfs record.
	/*  6*/ uint16_t usa_cnt;			// Number of le16 sized entries in the usa
};
#pragma pack(pop)

typedef struct _ntfs_record* ntfs_record;

#pragma pack (push, 1)
struct _raw_mft_record{
	/*  0*/ struct _ntfs_record;

	/*  8*/	uint64_t lsn;					// $LogFile sequence number for this record. Changed every time the record is modified.
	/* 16*/	uint16_t seq_nmbr;				// Number of times this mft record has been reused.
	/* 18*/	uint16_t link_cnt;				// Number of hard links, i.e. the number of directory entries referencing this record.
	/* 20*/	uint16_t attrs_offs;			// Byte offset to the first attribute in this mft record from the start of the mft record.
	/* 22*/	uint16_t flags;					// Bit array of fLAGS. When a file is deleted, the MFT_RECORD_IN_USE flag is set to zero.
	/* 24*/	uint32_t bytes_used;			// Number of bytes used in this mft record.
	/* 28*/	uint32_t bytes_alloc;			// Number of bytes allocated for this mft record. This should be equal to the mft record size.
	/* 32*/	uint64_t base_mft_rec;			// This is zero for base mft records. When it is not zero it is a mft reference
											// pointing to the base mft record to which this record belongs.
	/* 40*/	uint16_t next_attr_inst;		// The instance number that will be assigned to the next attribute added to this mft record.
	/* 42*/ uint16_t reserved;				// Reserved/alignment.
	/* 44*/ uint32_t mft_rec_number;		// Number of this mft record.
};
#pragma pack(pop)
typedef struct _raw_mft_record* raw_mft_record;

mft_record CreateMFTRecord();
bytes GetMFTRecord(const execution_context context, uint64_t index);
bytes BytesFromAttributeList(const execution_context context, const attribute attr, uint64_t index);

inline raw_mft_record MFTRecPtr(bytes buf) { return (raw_mft_record)(buf->buffer); }

bool SetUpMFTIndex(execution_context context)
{
	mft_record mft = AttributesForFile(context, MASTER_FILE_TABLE_NUMBER, AttrTypeFlag(ATTR_DATA));
	if (!mft)
	{
		printf("Error: Getting MFT record 0.\n");
		return false;
	}

	UT_array* attribs = (mft->attributes[AttrTypeToIndex(ATTR_DATA)]);
	context->mft_index = EmptyRunList();
	for (bytes at = utarray_front(attribs); at; at = utarray_next(attribs, at))
	{
		attribute attr = AttributePtr(at, 0);
		if (attr->non_resident)
		{
			uint64_t TotClusters = attr->end_vcn - attr->start_vcn + 1;
			uint64_t actual_sz = attr->real_sz;
			bytes next;

			ExtractRunsFromAttribute(context->mft_index, attr);
			for (next = utarray_next(attribs, at); next != NULL && 
				TotClusters * BytesPerCluster(context->boot) < actual_sz; next = utarray_next(attribs, next))
			{
				attribute attr = AttributePtr(next, 0);
				TotClusters += attr->end_vcn - attr->start_vcn + 1;
				ExtractRunsFromAttribute(context->mft_index, attr);
			}
			if (!next)
				break;
			else
				at = next;
		}
	}

	DeleteMFTRecord(mft);
	return true;
}

bool SetUppercaseList(execution_context context) 
{
	context->upper_case = malloc(0x10000 * sizeof(wchar_t));
	if (!context->upper_case)
	{
		printf("Memory allocation error.\n");
		exit(-1);
	}
	mft_record mft = AttributesForFile(context, UPCASE_TABLE_NUMBER, AttrTypeFlag(ATTR_DATA));
	if (!mft)
	{
		printf("Error: Getting MFT record 10.\n");
		return false;
	}

	rsize_t char_index = 0;
	UT_array* data = GetAttributes(ATTR_DATA, mft);
	bytes raw_at;
	if (!data || !(raw_at = utarray_front(data)))
	{
		printf("Error: UpCase file incomplete.\n");
		return false;
	}
	uint32_t file_sz = (uint32_t)min(AttributePtr(raw_at, 0)->real_sz, 0x10000*sizeof(wchar_t));
	for (; raw_at && char_index*sizeof(wchar_t) < file_sz; raw_at = utarray_next(data, raw_at))
	{
		bytes at_data = GetAttributeData(context->cr, AttributePtr(raw_at, 0));
		rsize_t copy_len = min((file_sz - char_index) * sizeof(wchar_t), at_data->buffer_len);
		memcpy((uint8_t*)context->upper_case + char_index * sizeof(wchar_t), at_data->buffer, copy_len);
		char_index += copy_len / sizeof(wchar_t);
		DeleteBytes(at_data);
	}
	DeleteMFTRecord(mft);
	return char_index == 0x10000;
}

bool DoFixUp(bytes record, uint16_t sector_sz)
{
	ntfs_record hdr = (ntfs_record)(record->buffer);

	for (uint32_t i = 1; i < hdr->usa_cnt; ++i)
		if (!Equals(record, hdr->usa_offs, record, (uint64_t)sector_sz * i - 2, 2))
			return false;

	for (uint32_t i = 1; i < hdr->usa_cnt; ++i)
		Patch(record, (uint64_t)sector_sz * i - 2, record, hdr->usa_offs + 2ULL * i, 2);

	return true;
}

mft_record AttributesForFile(execution_context context, uint64_t mft_ref, uint32_t attribute_mask)
{
	mft_ref &= 0x0000FFFFFFFFFFFF;

	bytes rec = GetMFTRecord(context, mft_ref);
	if (!rec)
		return NULL;
	mft_record result = CreateMFTRecord();

	for (uint32_t attrib_offs = MFTRecPtr(rec)->attrs_offs; ;)
	{
		attribute attr = AttributePtr(rec, attrib_offs);
		if (attr->type == ATTR_ATTRIBUTE_END_MARKER)
			break;

		//Push it on to the relevant queue:
		if (AttrTypeFlag(attr->type) & attribute_mask)
			AppendBuffer(result->attributes[AttrTypeToIndex(attr->type)], rec, attrib_offs,attr->length);

		//Offset for next iteration:
		attrib_offs += attr->length;

		//Handle the special cases:
		if (attr->type == ATTR_ATTRIBUTE_LIST)
		{
			bytes extra_at = BytesFromAttributeList(context, attr, mft_ref);
			//This destroys the integrity of our copy the MFT because, we remove/overwrite the CRC at the end
			AppendAt(rec, MFTRecPtr(rec)->bytes_used - 8, extra_at, 0, extra_at->buffer_len);
			DeleteBytes(extra_at);
		}
	}
	DeleteBytes(rec);

	return result;
}

bytes BytesFromAttributeList(const execution_context context, const attribute attr, uint64_t index)
{
	const static struct _bytes end_marker = { "\xff\xff\xff\xff", 4 };
	bytes result = CreateEmpty();

	UT_array* attrs = GetAttributeListRecords(context->cr, index, attr);
	for (uint64_t* recID = utarray_front(attrs); recID; recID = utarray_next(attrs, recID))
	{
		bytes rec = GetMFTRecord(context, (*recID) & 0x0000FFFFFFFFFFFF);
		//Last 4 bytes are the end marker FFFFFFFF followed by a 4 byte CRC, so we don't copy these:
		Append(result, rec, MFTRecPtr(rec)->attrs_offs, MFTRecPtr(rec)->bytes_used - MFTRecPtr(rec)->attrs_offs - 8);
		DeleteBytes(rec);
	}
	utarray_free(attrs);
	AppendAt(result, result->buffer_len, (const bytes)&end_marker, 0, 4);
	return result;
}

UT_array* GetAttributes(uint32_t attrib_type, const mft_record mft)
{
	return mft->attributes[AttrTypeToIndex(attrib_type)];
}


wchar_t* FileNameFromMFTRec(const mft_record mft)
{
	UT_array* name_lst = GetAttributes(ATTR_FILE_NAME, mft);
	if (!name_lst || utarray_len(name_lst) == 0)
		return NULL;

	bytes raw_at = utarray_front(name_lst);
	attribute cur = NULL;
	for (; raw_at; raw_at = utarray_next(name_lst, raw_at))
	{
		cur = AttributePtr(raw_at,0);
		if (NameSpaceFromNameAttr(cur) != FILE_NAME_DOS)
			break;
	}
	if (!cur)
		cur = AttributePtr(utarray_front(name_lst), 0);
	if (!cur)
		return NULL;
	return NameFromNameAttr(cur);
}

bytes GetMFTRecord(const execution_context context, uint64_t index)
{
	bytes result = NULL;
	if (index == 0)
		//This is the bootstrap case, where we simply need to read record 0:
		result = ReadImageBytes(context->cr, context->boot->logical_clust_num_for_mft *
			BytesPerCluster(context->boot), MFTRecordSize(context->boot));
	else
		result = BlockFromRunList(context->cr, context->mft_index, context->boot->clusters_per_file_record, index);

	if (!result)
		return ErrorCleanUp(NULL, NULL, "Unexisting record index: %lld\n", index);

	if (!DoFixUp(result, context->boot->bytes_per_sector))
	{
		wprintf(L"%lld: The record failed Fixup: \n", index);
		WriteToFile(stdout, result);
		return ErrorCleanUp(DeleteBytes, result, "\n");
	}

	//Now recast it to an actual MFT record and do some further checks:
	raw_mft_record rec = (raw_mft_record)result->buffer;

	if (strncmp(rec->magic, (unsigned char*)RecordSignature, 4))
		return ErrorCleanUp(DeleteBytes, result, "MFT record signature not found.\n");

	if (!(rec->flags & MFT_RECORD_IN_USE))
		return ErrorCleanUp(DeleteBytes, result, "MFT record not found: %lld\n", index);

	if (rec->mft_rec_number != index)
		return ErrorCleanUp(DeleteBytes, result, "Corrupt MFT record for index: %lld\n", index);

	return result;
}

mft_record CreateMFTRecord()
{
	SafeCreate(result, mft_record);
	
	
	for (int i = 0; i < 16; ++i)
		result->attributes[i] = CreateBufferList();

	return result;
}

void DeleteMFTRecord(mft_record rec)
{
	for (int i = 0; i < 16; ++i)
		utarray_free(rec->attributes[i]);
	free(rec);
}