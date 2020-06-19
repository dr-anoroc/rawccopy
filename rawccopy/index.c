#include "index.h"
#include "mft.h"
#include "attribs.h"
#include "datarun.h"


#define COLLATION_FILE_NAME 0x01


#define INDEX_ENTRY_NODE 0x0001 /* This entry contains a sub-node, i.e. a reference to an index block in form of
								   a virtual cluster number */
#define	INDEX_ENTRY_END 0x0002  /* This signifies the last entry in an index block.  The index entry does not 
								   represent a file but it can point to a sub-node. */

#pragma pack (push, 1)
struct _index_header
{
	/*Ox00*/ uint32_t entries_offs;		/* Byte offset to first INDEX_ENTRY
											aligned to 8-byte boundary. */
	/*0x04*/ uint32_t index_length;			/* Data size of the index in bytes,
											i.e. bytes used from allocated
											size, aligned to 8-byte boundary. */
	/*0x08*/ uint32_t allocated_sz;		/* Byte size of this index (block),
											multiple of 8 bytes. */
	/*0x0c*/ uint8_t flags;					/* Bit field of INDEX_HEADER_FLAGS. */
	/*0x0d*/ uint8_t reserved[3];			/* Reserved/align to 8-byte boundary. */
};
#pragma pack(pop)
typedef struct _index_header* index_header;

#pragma pack (push, 1)
struct _index_root
{
	/*0x00*/ uint32_t type;						/* Type of the indexed attribute. Is ATTR_FILE_NAME for directories, zero
												   for view indexes. No other values allowed. */
	/*0x04*/ uint32_t collation_rule;			/* Collation rule used to sort the index entries. If type is ATTR_FILE_NAME,
												   this must be COLLATION_FILE_NAME. */
	/*0x08*/ uint32_t index_block_sz;			/* Size of each index block in bytes (in the index allocation attribute). */
	/*0x0c*/ uint8_t clusters_per_index_block;	/* Cluster size of each index block (in the index allocation attribute), when
												   an index block is >= than a cluster, otherwise this will be the log of
												   the size (like how the encoding of the mft record size and the index
												   record size found in the boot sector work). Has to be a power of 2. */
	/*Ox0d*/ uint8_t reserved[3];				/* Reserved/align to 8-byte boundary. */
	/*0x10*/ struct _index_header header;		/* Describes the index entries. */
};
#pragma pack(pop)
typedef struct _index_root* index_root;

#pragma pack (push, 1)
struct _index_block {
	struct {
		/*0x00*/ uint8_t magic[4];			// A four-byte magic identifying the record, should be "INDX". 
		/*0x04*/ uint16_t usa_offs; 		// Offset to the Update Sequence Array from the start of the ntfs record.
		/*0x06*/ uint16_t usa_cnt;			// Number of le16 sized entries in the usa
	};

	/*0x08*/ uint64_t lsn;					/* $LogFile sequence number of the last modification of this index block. */
	/*0x10*/ uint64_t index_block_vcn;		/* Virtual cluster number of the index block. If the cluster_size on the
											   volume is <= the index_block_size of the directory, index_block_vcn counts
											   in units of clusters, and in units of sectors otherwise. */
	/*0x18*/ struct _index_header header;	/* Describes the index entries. */
};
#pragma pack(pop)
typedef struct _index_block* index_block;

struct _index
{
	bytes root;
	UT_array* records;
};
typedef struct _index* index;

struct _index_iter
{
	index root_index;
	UT_array* work_queue;
};

struct {
	uint32_t flag;
	wchar_t* description;
} const flag_desc[] =
{
	{FILE_ATTR_READONLY, L"read-only"},
	{FILE_ATTR_HIDDEN, L"hidden"},
	{FILE_ATTR_SYSTEM, L"system"},
	{FILE_ATTR_DIRECTORY, L"directory"},
	{FILE_ATTR_ARCHIVE, L"archive"},
	{FILE_ATTR_DEVICE, L"device"},
	{FILE_ATTR_NORMAL, L"normal"},
	{FILE_ATTR_TEMPORARY, L"temporary"},
	{FILE_ATTR_SPARSE_FILE, L"sparse-file"},
	{FILE_ATTR_REPARSE_POINT, L"reparse-point"},
	{FILE_ATTR_COMPRESSED, L"compressed"},
	{FILE_ATTR_OFFLINE, L"offline"},
	{FILE_ATTR_NOT_CONTENT_INDEXED, L"not-indexed"},
	{FILE_ATTR_ENCRYPTED, L"encrypted"},
	{FILE_ATTRIBUTE_INTEGRITY_STREAM, L"integrity-stream"},
	{FILE_ATTRIBUTE_VIRTUAL, L"virtual"},
	{FILE_ATTRIBUTE_NO_SCRUB_DATA, L"no-scrub-data"},
	{FILE_ATTR_DUP_FILE_NAME_INDEX_PRESENT, L"directory"},
	{FILE_ATTR_DUP_VIEW_INDEX_PRESENT, L"index-view"}	
};

#define HeaderFromRawRoot(root) ((index_header) &((index_root)(root->buffer))->header)
#define HeaderFromRawBlock(block) ((index_header) &((index_block)(block->buffer))->header)
#define FirstEntryFromHeader(hdr) ((index_entry)((uint8_t *)hdr + hdr->entries_offs))
#define NextEntry(entry) (index_entry)((uint8_t *)entry + entry->entry_size)

void QueueNode(index_iter it, const index_header hdr);
void GoLeft(index_iter it);

index IndexForFolder(const execution_context context, uint64_t mft_ref);
bytes FindIndexRoot(const mft_record rec);
bytes FindIndexAllocations(const execution_context context, const mft_record rec);
index_entry FindName(const execution_context context, const index ind, const wchar_t* name);

index_entry EntryFromIndex(const index_header hdr, const UT_array* entry_lst, uint32_t ind);

void DeleteIndex(index ind);

int CompareName(const execution_context context, const wchar_t* name, const index_entry entry);


#define EntryInNode(hdr, offset) ((index_entry)(((uint8_t*)(hdr) + (offset))))

wchar_t* FileNameFromIndex(const index_entry rec)
{
	return NtfsNameExtract(rec->filename, rec->filename_len);
}

index_iter StartIndexIterator(const execution_context context, const index_entry root)
{
	index ind = IndexForFolder(context, root->mft_reference);
	if (!ind)
		return NULL;
	SafeCreate(result, index_iter);

	result->root_index = ind;
	utarray_new(result->work_queue, &ut_ptr_icd);
	QueueNode(result, (index_header)(ind->root->buffer + 0x10));
	GoLeft(result);
	return result;
}

void CloseIndexIterator(index_iter iter)
{
	DeleteIndex(iter->root_index);
	utarray_free(iter->work_queue);
	free(iter);
}

index_entry CurrentIterEntry(const index_iter iter)
{
	if (utarray_len(iter->work_queue) == 0)
		return NULL;

	index_entry* head = ((index_entry*)utarray_back(iter->work_queue));
	return (head ? *head : NULL);
};

index_entry NextIterEntry(index_iter iter)
{
	if (utarray_len(iter->work_queue) > 0)
	{
		utarray_pop_back(iter->work_queue);
		GoLeft(iter);
	}
	return CurrentIterEntry(iter);
};

void QueueNode(index_iter it, const index_header hdr)
{
	long pos = utarray_len(it->work_queue);

	for (index_entry rec_ptr = FirstEntryFromHeader(hdr);
		(uint8_t* )rec_ptr < (uint8_t*)hdr + hdr->index_length; rec_ptr = NextEntry(rec_ptr))
		utarray_insert(it->work_queue, &rec_ptr, (rsize_t)pos);
}

void GoLeft(index_iter it)
{
	do
	{
		index_entry head = CurrentIterEntry(it);
		if (!head)
			return;

		if (head->index_flags & INDEX_ENTRY_NODE)
		{
			bytes subnode = utarray_eltptr(it->root_index->records, SubNodeEntry(head));
			if (!subnode)
				break;
			QueueNode(it, (index_header)(subnode->buffer + 0x18));
		}
		else
			break;
	} while (true);
	while (utarray_len(it->work_queue) > 0 && (CurrentIterEntry(it)->index_flags & INDEX_ENTRY_END))
		utarray_pop_back(it->work_queue);
}

uint64_t SubNodeEntry(const index_entry rec)
{
	return *(uint64_t*)((uint8_t*)rec + rec->entry_size - 8);
}

bytes FindIndexEntry(const execution_context context, uint64_t parent_mft, const wchar_t* name)
{
	index ind = IndexForFolder(context, parent_mft);
	if (!ind)
		return ErrorCleanUp(NULL, NULL, "Error: cannot find index for mft: %llx\n", parent_mft);

	index_entry hit = FindName(context, ind, name);
	bytes result = NULL;
	if (!hit)
	{
		wprintf(L"Error: Unable to find the file %ls by index scanning: \n", name);
	}
	else
	{
		result = FromBuffer((void*)hit, hit->entry_size);
	}
	DeleteIndex(ind);
	return result;
}

index_entry FindName(const execution_context context, const index ind, const wchar_t* name)
{
	UT_array* entry_lst;
	utarray_new(entry_lst, &ut_ptr_icd);

	for (index_header hdr = HeaderFromRawRoot(ind->root);;)
	{
		for (uint32_t offs = hdr->entries_offs; offs < hdr->index_length; offs += EntryInNode(hdr, offs)->entry_size)
			utarray_push_back(entry_lst, &offs);

		int32_t s = 0;
		int32_t e = utarray_len(entry_lst) - 1;
		int32_t c;
		index_entry cur = NULL;
		int cmp;
		while (e > s)
		{
			c = s + (e - s) / 2;
			if (!(cur = EntryFromIndex(hdr, entry_lst, c)))
				break;

			cmp = CompareName(context, name, cur);

			if (cmp == 0)
			{
				utarray_free(entry_lst);
				return cur;
			}
			else if (cmp < 0)
				e = c;
			else
				s = c + 1;
		}

		if (!(cur = EntryFromIndex(hdr, entry_lst, s)) || !(cur->index_flags & INDEX_ENTRY_NODE))
			break;

		bytes subnode = utarray_eltptr(ind->records, SubNodeEntry(cur));
		if (!subnode)
			break;
		hdr = (index_header)(subnode->buffer + 0x18);
		utarray_clear(entry_lst);
	}
	utarray_free(entry_lst);
	return NULL;
}

wchar_t* FileFlagsFromIndexRec(const index_entry rec)
{
	wchar_t* result = DuplicateString(L"");
	uint64_t fl_cpy = rec->file_flags;

	for (int i = 0; i < 19 && fl_cpy; ++i)
	{
		if (rec->file_flags & flag_desc[i].flag)
			StringAssign(result, JoinStrings(L" | ", result, flag_desc[i].description, NULL));
		fl_cpy &= (~flag_desc[i].flag);
	}
	return result;
}

index_entry EntryFromIndex(const index_header hdr, const UT_array *entry_lst, uint32_t ind)
{
	uint32_t* entr_i = (uint32_t*)utarray_eltptr(entry_lst, ind);
	if (!entr_i)
		return NULL;
	return EntryInNode(hdr, *entr_i);
}

index IndexForFolder(const execution_context context, uint64_t mft_ref)
{
	mft_record rec = AttributesForFile(context, mft_ref, AttrTypeFlag(ATTR_INDEX_ROOT) | AttrTypeFlag(ATTR_INDEX_ALLOCATION));
	if (!rec)
		return ErrorCleanUp(NULL, NULL, "Problem finding index root: %lld\n", mft_ref);

	SafeCreate(result, index);

	result->root = FindIndexRoot(rec);
	result->records = NULL;
	if (!result->root)
	{
		DeleteIndex(result);
		return ErrorCleanUp(DeleteMFTRecord, rec, "Problem finding index root: %lld\n", mft_ref);
	}

	if (HeaderFromRawRoot(result->root)->flags)
	{
		//index entries are in index allocation attribute
		bytes allocs = FindIndexAllocations(context, rec);
		if (!allocs)
		{
			DeleteMFTRecord(rec);
			return ErrorCleanUp(DeleteIndex, result, "Problem finding index allocation nodes: %lld\n", mft_ref);
		}

		index_root root = (index_root)(result->root->buffer);
		
		result->records = CreateBufferList();
		bytes empty_buf = CreateEmpty();
		bool success = true;
		for (uint32_t offset = 0; offset < allocs->buffer_len; offset += root->index_block_sz)
		{
			if (offset + root->index_block_sz > (uint32_t)allocs->buffer_len)
			{
				printf("Encountered incomplete index block.\n");
				success = false;
				break;
			}
			index_block block = (index_block)(allocs->buffer + offset);
			if (strncmp(block->magic, "INDX", 4))
			{
				//printf("Encountered index block without magic header. Skipping it.\n");
				continue;
			}

			//This is a bit more convoluted than what we should realistically expect,
			//but there is nothing in the specs about the fact that the index sequences
			//appear in the order indicated by their respective index_block_vcn
			while (utarray_len(result->records) <= (uint32_t)block->index_block_vcn)
				AppendBuffer(result->records, empty_buf, 0, 0);

			bytes array_bl = (bytes)utarray_eltptr(result->records, (uint32_t)block->index_block_vcn);

			if (!array_bl)
			{
				success = false;
				break;
			}
			//Just to be sure, erase what's already there:
			RightTrim(array_bl, array_bl->buffer_len);
			Append(array_bl, allocs, offset, root->index_block_sz);
			if (!DoFixUp(array_bl, context->boot->bytes_per_sector))
			{
				success = false;
				break;
			}
		}
		DeleteBytes(empty_buf);
		DeleteBytes(allocs);
		if (!success)
		{
			DeleteIndex(result);
			DeleteMFTRecord(rec);
			return NULL;
		}
	}
	DeleteMFTRecord(rec);
	return result;
}

bytes FindIndexRoot(const mft_record rec)
{
	UT_array* att_lst = GetAttributes(ATTR_INDEX_ROOT, rec);

	if (!att_lst)
		return NULL;

	wchar_t* att_name = NULL;
	bytes result = NULL;
	for (bytes at = utarray_front(att_lst); at; at = utarray_next(att_lst, at))
	{
		StringAssign(att_name, GetAttributeName(AttributePtr(at,0)));
		if (!wcsncmp(att_name, L"$I30", 4))
		{
			attribute attr = AttributePtr(at, 0);
			result = TakeBufferSlice(at, attr->value_offs, attr->length - attr->value_offs);
			break;
		}
	}
	if (att_name)
		free(att_name);
	return result;
}

bytes FindIndexAllocations(const execution_context context, const mft_record rec)
{
	UT_array* att_lst = GetAttributes(ATTR_INDEX_ALLOCATION, rec);

	if (!att_lst)
		return NULL;

	wchar_t* att_name = NULL;
	bytes result = CreateEmpty();
	bytes buf = NULL;
	for (bytes at = utarray_front(att_lst); at; at = utarray_next(att_lst, at))
	{
		attribute att = AttributePtr(at, 0);
		StringAssign(att_name, GetAttributeName(att));
		if (!wcsncmp(att_name, L"$I30", 4))
		{
			buf = GetAttributeData(context->cr, att);
			Append(result, buf, 0, buf->buffer_len);
			DeleteBytes(buf);
		}
	}
	if (result->buffer_len == 0)
	{
		DeleteBytes(result);
		result = NULL;
	}
	if (att_name)
		free(att_name);
	return result;
}

void DeleteIndex(index ind)
{
	DeleteBytes(ind->root);
	if (ind->records)
		utarray_free(ind->records);
	free(ind);
}

int CompareName(const execution_context context, const wchar_t* name, const index_entry entry)
{
	if (entry->index_flags & INDEX_ENTRY_END)
		return -1;

	for (wchar_t *ind_name = (wchar_t *)entry->filename; ind_name < (wchar_t *)entry->filename + (rsize_t)entry->filename_len;)
	{
		if (!*name)
			//End of given name, but not index name:
			return -1;
		int dif = context->upper_case[*name++] - context->upper_case[*ind_name++];
		if (dif != 0)
			return dif;
	}
	//End of index name:
	return !*name ? 0 : 1;
}

