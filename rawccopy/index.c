#include "index.h"
#include "mft.h"
#include "attribs.h"


#define COLLATION_FILE_NAME 0x01


#define INDEX_ENTRY_NODE 0x0001 /* This entry contains a sub-node, i.e. a reference to an index block in form of
								   a virtual cluster number */
#define	INDEX_ENTRY_END 0x0002  /* This signifies the last entry in an index block.  The index entry does not 
								   represent a file but it can point to a sub-node. */

#pragma pack (push, 1)
typedef struct _index_header
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
}*index_header;
#pragma pack(pop)

#pragma pack (push, 1)
typedef struct _index_root
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
}*index_root;
#pragma pack(pop)

#pragma pack (push, 1)
typedef struct _index_block {
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
} *index_block;
#pragma pack(pop)


struct _index_iter{
	mft_file folder;
	attribute_reader alloc_rdr;
	UT_array* node_queue;
	UT_array* index_queue;
	uint64_t vcn_mult;
	uint64_t block_sz;
	rsize_t index_depth;
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

#define HeaderFromRawBlock(block) ((index_header) &((index_block)((block)->buffer))->header)

#define IndexNodeFromRootAttrib(attrib) ((index_header) &(((index_root)((uint8_t*)attrib + attrib->value_offs))->header))
#define FirstIndexEntry(hdr) ((index_entry)((uint8_t *)hdr + hdr->entries_offs))
#define NextIndexEntry(hdr, cur) (index_entry)(((uint8_t *)(cur) + (cur)->entry_size) <  \
			(uint8_t*)(hdr) + (hdr)->index_length ? ((uint8_t *)(cur) + (cur)->entry_size) : NULL )

void QueueNode(execution_context context, index_iter it, uint64_t index_vcn);
void GoLeft(execution_context context, index_iter it);

bool AppendIndexBlock(execution_context context, attribute_reader rdr, uint64_t offset,
					uint64_t block_sz, bytes dest, uint64_t pos);

bool IndexSetup(execution_context context, mft_file file, attribute* root, attribute_reader* alloc_rdr, uint64_t *block_sz, uint64_t *vcn_mult);

int CompareName(const void* name, const void* entry, void* context);

#define EntryInNode(hdr, offset) ((index_entry)(((uint8_t*)(hdr) + (offset))))


index_entry NextIterEntry(execution_context context, index_iter iter)
{
	if (iter->index_depth > 0)
	{
		bytes* buf_ptr = utarray_eltptr(iter->node_queue, iter->index_depth - 1);
		index_entry* ent = utarray_eltptr(iter->index_queue, iter->index_depth - 1);

		if (!buf_ptr || !ent)
			return NULL;

		*ent = NextIndexEntry((index_header)(*buf_ptr)->buffer, *ent);
		GoLeft(context, iter);
	}
	return CurrentIterEntry(iter);
};

void QueueNode(execution_context context, index_iter it, uint64_t index_vcn)
{
	if (++(it->index_depth) >= utarray_len(it->node_queue))
	{
		bytes q_buf = CreateBytes((rsize_t)it->block_sz);
		utarray_push_back(it->node_queue, &q_buf);
		utarray_extend_back(it->index_queue);
	}
	bytes* buf_ptr = utarray_eltptr(it->node_queue, it->index_depth - 1);
	index_entry *ent = utarray_eltptr(it->index_queue, it->index_depth - 1);
	if (!buf_ptr || !ent)
		return;

	AppendIndexBlock(context, it->alloc_rdr, index_vcn * it->vcn_mult,
		it->block_sz, *buf_ptr, 0);

	*ent = FirstIndexEntry(HeaderFromRawBlock(*buf_ptr));
}

index_entry CurrentIterEntry(const index_iter iter)
{
	if (iter->index_depth <= 0)
		return NULL;

	index_entry *ent_ptr = utarray_eltptr(iter->index_queue, iter->index_depth - 1);
	return (ent_ptr ? *ent_ptr : NULL);
}

void GoLeft(execution_context context, index_iter it)
{
	do
	{
		index_entry head = CurrentIterEntry(it);
		if (!head)
			return;

		if (head->index_flags & INDEX_ENTRY_NODE)
			QueueNode(context, it, SubNodeEntry(head));
		else
			break;
	} while (true);

	while (it->index_depth > 0 && CurrentIterEntry(it)->index_flags & INDEX_ENTRY_END)
		it->index_depth--;
}


index_iter StartIndexIterator(execution_context context, const index_entry root)
{
	mft_file file = LoadMFTFile(context, root->mft_reference);
	if (!file)
		return NULL;

	SafeCreate(result, index_iter);
	result->folder = file;

	result->node_queue = ListOfBuffers();
	utarray_new(result->index_queue, &ut_ptr_icd);
	result->alloc_rdr = NULL;
	attribute index_rt = NULL;
	if (!IndexSetup(context, file, &index_rt, &result->alloc_rdr, &result->block_sz, &result->vcn_mult))
		return ErrorCleanUp(CloseIndexIterator, result, "");

	bytes root_node = FromBuffer(IndexNodeFromRootAttrib(index_rt), IndexNodeFromRootAttrib(index_rt)->index_length + sizeof(struct _index_header));
	utarray_push_back(result->node_queue, &root_node);
	index_entry first = FirstIndexEntry(IndexNodeFromRootAttrib(index_rt));
	utarray_push_back(result->index_queue, &first);

	result->index_depth = 1;

	GoLeft(context, result);
	return result;
};

void CloseIndexIterator(index_iter iter)
{
	DeleteMFTFile(iter->folder);
	if (iter->alloc_rdr)
		CloseAttributeReader(iter->alloc_rdr);
	utarray_free(iter->node_queue);
	utarray_free(iter->index_queue);
	free(iter);
}


uint64_t SubNodeEntry(const index_entry rec)
{
	return *(uint64_t*)((uint8_t*)rec + rec->entry_size - 8);
}

bytes FindIndexEntry(execution_context context, uint64_t parent_mft, const wchar_t* name)
{
	mft_file rec = LoadMFTFile(context, parent_mft);
	if (!rec)
		return ErrorCleanUp(NULL, NULL, "Problem finding index root: %lld\n", parent_mft);

	attribute root = NULL;
	attribute_reader alloc_rdr;
	uint64_t block_sz = 0;
	uint64_t vcn_mult = 0;

	if (!IndexSetup(context, rec, &root, &alloc_rdr, &block_sz, &vcn_mult))
		return ErrorCleanUp(DeleteMFTFile, rec, "");

	UT_array* entry_lst;
	utarray_new(entry_lst, &ut_ptr_icd);

	bytes index_bl = CreateEmpty();
	bytes result = NULL;

	//First node comes from root:
	for (index_header hdr = IndexNodeFromRootAttrib(root); ; hdr = HeaderFromRawBlock(index_bl))
	{
		//Scan through the individual entries and put them in an ut_array for binary search
		for (index_entry entr = FirstIndexEntry(hdr); entr; entr = NextIndexEntry(hdr, entr))
			utarray_push_back(entry_lst, &entr);

		int ind = FindInArray(entry_lst, name, context, CompareName);
		if (ind >= 0)
		{
			index_entry* hit = utarray_eltptr(entry_lst, (rsize_t)ind);
			if (hit)
				result = FromBuffer((void*)*hit, (*hit)->entry_size);
			break;
		}
		else
		{
			//Nothing found: if it's an end node, search failed,
			//otherwise need to go deeper
			index_entry* hit = utarray_eltptr(entry_lst, (rsize_t)~ind);
			if (!hit)
				break;
			if ((*hit)->index_flags & INDEX_ENTRY_NODE)
			{
				if (!alloc_rdr || !AppendIndexBlock(context, alloc_rdr, SubNodeEntry(*hit) * vcn_mult,
										block_sz, index_bl, 0))
					break;
			}
			else
			{
				wprintf(L"Error: Unable to find the file %ls by index scanning: \n", name);
				break;
			}
		}
		utarray_clear(entry_lst);
	}
	if (alloc_rdr)
		CloseAttributeReader(alloc_rdr);

	DeleteBytes(index_bl);
	DeleteMFTFile(rec);
	utarray_free(entry_lst);
	return result;
}

bool IndexSetup(execution_context context, mft_file file, attribute* root, attribute_reader* alloc_rdr, uint64_t* block_sz, uint64_t* vcn_mult)
{
	*root = NULL;
	for (*root = FirstAttribute(context, file, AttrTypeFlag(ATTR_INDEX_ROOT)); *root;
		*root = NextAttribute(context, file, *root, AttrTypeFlag(ATTR_INDEX_ROOT)))
	{
		if (!wcsncmp((wchar_t*)((uint8_t*)*root + (*root)->name_offs), L"$I30", (*root)->name_len))
			break;
	}

	if (!*root)
		return CleanUpAndFail(NULL, NULL, "Corrupt index: no root found.\n");

	index_root tmp1 = (index_root)((uint8_t*)(*root) + (*root)->value_offs);

	*block_sz = ((index_root)((uint8_t*)(*root) + (*root)->value_offs))->index_block_sz;
	*vcn_mult = context->cluster_sz <= *block_sz ? (uint64_t)context->cluster_sz : (uint64_t)context->boot->bytes_per_sector;

	if (!((index_root)((uint8_t*)(*root) + (*root)->value_offs))->header.flags)
	{
		*alloc_rdr = NULL;
		return true;
	}

	attribute allocs = NULL;
	for (allocs = FirstAttribute(context, file, AttrTypeFlag(ATTR_INDEX_ALLOCATION)); allocs;
		allocs = NextAttribute(context, file, allocs, AttrTypeFlag(ATTR_INDEX_ALLOCATION)))
	{
		if (!wcsncmp((wchar_t*)((uint8_t*)allocs + allocs->name_offs), L"$I30", allocs->name_len))
			break;
	}
	if (!allocs)
		return CleanUpAndFail(NULL, NULL, "Index should contain index allocations, but none found.\n");

	*alloc_rdr = OpenAttributeReader(context, file, allocs);
	return true;
}


bool AppendIndexBlock(execution_context context, attribute_reader rdr, uint64_t offset,
						uint64_t block_sz, bytes dest, uint64_t pos)
{
	if (!AppendBytesFromAttribRdr(context, rdr, offset, block_sz, dest, 0))
		return false;

	index_block block = (index_block)(dest->buffer + pos);
	if (strncmp(block->magic, "INDX", 4))
		return CleanUpAndFail(NULL, NULL, "Encountered index block without magic header.\n");

	if (!DoFixUp(dest, context->boot->bytes_per_sector))
		return CleanUpAndFail(NULL, NULL, "Encountered corrupt index block.\n");

	return true;
}


bool FileFlagsFromIndexRec(const index_entry rec, string dest)
{
	uint64_t fl_cpy = rec->file_flags;
	ClearString(dest);
	for (int i = 0; i < 19 && fl_cpy; ++i)
	{
		if (rec->file_flags & flag_desc[i].flag)
		{
			if (StringLen(dest) > 0)
				StringPrint(dest, StringLen(dest), L" | %ls", flag_desc[i].description);
			else
				StringPrint(dest , 0, L"%ls", flag_desc[i].description);

			fl_cpy &= (~flag_desc[i].flag);
		}
	}
	return true;
}



int CompareName(const void* name, const void* entry, void* context)
{
	if ((*(index_entry*)entry)->index_flags & INDEX_ENTRY_END)
		return -1;

	for (wchar_t *ind_name = (wchar_t *)(*(index_entry*)entry)->filename;
		  ind_name < (wchar_t *)(*(index_entry*)entry)->filename + (rsize_t)(*(index_entry*)entry)->filename_len;)
	{
		if (!*((wchar_t*)name))
			//End of given name, but not index name:
			return -1;
		int dif = ((execution_context)context)->upper_case[*((wchar_t*)name)++] - ((execution_context)context)->upper_case[*ind_name++];
		if (dif != 0)
			return dif;
	}
	//End of index name:
	return !*((wchar_t*)name) ? 0 : 1;
}
