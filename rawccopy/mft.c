#include "mft.h"
#include "disk-info.h"
#include "attribs.h"

const unsigned char RecordSignature[] = { 0x46, 0x49, 0x4C, 0x45 }; //FILE signature, is actually "FILE"
const unsigned char RecordSignatureBad[] = { 0x44, 0x41, 0x41, 0x42 }; // BAAD signature


#define MFT_RECORD_IN_USE 0x0001
#define MFT_RECORD_IS_DIRECTORY 0x0002

#pragma pack (push, 1)
typedef struct _ntfs_record {
	/*  0*/ uint8_t magic[4];			// A four-byte magic identifying the record, contains strings like "FILE" or "INDX"
	/*  4*/ uint16_t usa_offs;			// Offset to the Update Sequence Array from the start of the ntfs record.
	/*  6*/ uint16_t usa_cnt;			// Number of le16 sized entries in the usa
}*ntfs_record;
#pragma pack(pop)

#pragma pack (push, 1)
typedef struct _raw_mft_record{
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
}* raw_mft_record;
#pragma pack(pop)

struct _mft_file {
	UT_array* mft_recs;			// array holding actual mft records, sorted by record number
	bytes at_list;				// byte buffer the individual entries in the attribute list 
};

struct _attribute_reader {
	uint64_t position;			// the position (in bytes) within attribute
	mft_file parent;			// mft_file of which the attribute is a part of
	attribute attrib;			// the attribute that is being read
	bytes mft_sub_rec;			// mft_record holding the current attribute extent, only used when parent uses an Attribute List
	at_list_entry entry;		// attribute list entry that points to extent, only used for caching purposes when parent uses an Attribute List
	attribute extent;			// current attribute extent on which the reader is positioned; is equal to attrib, when parent doesn't use Attribute List
	run_list_iterator iter;		// iterates through the run list in extent if it is non-resident
	bytes compr_buf;			// buffer to be used in case of a compressed attribute, contains decompressed bytes
};

void SetFirstExtent(execution_context context, attribute_reader rdr);

void SetNextExtent(execution_context context, attribute_reader rdr);

bool AppendBytesFromRuns(execution_context context, const attribute attr, uint64_t cnt, bytes dest, uint64_t pos);

bool AppendNormalBytesFromAttribRdr(execution_context context, attribute_reader rdr, int64_t offset, uint64_t cnt, bytes dest, rsize_t pos);

bool AppendCompressedBytesFromAttribRdr(execution_context context, attribute_reader rdr, int64_t offset, uint64_t cnt, bytes dest, rsize_t pos);

//Simple forward iterator through all the attributes in an MFT file record:
attribute FirstAttr(bytes mft_rec);
attribute NextAttr(bytes mft_rec, const attribute cur);

//Some housekeeping functions for the record list in mft_rec:

#define FirstAttributeListEntry(file) (at_list_entry)(((mft_file)file)->at_list ? ((mft_file)file)->at_list->buffer : NULL)

#define NextAttributeListEntry(file, at) (at_list_entry)(!((mft_file)file)->at_list ? NULL : (!at ? NULL :									\
			(((char *)at + ((at_list_entry)at)->length >= ((mft_file)file)->at_list->buffer + ((mft_file)file)->at_list->buffer_len) ? NULL :	\
				(char *)at + ((at_list_entry)at)->length)))


//Looks for an mft record with id == index the mft_file and returns it as a bytes
//buffer. If it hasn't been cached, retrieves it from disk first.
bytes RetrieveSubMFT(execution_context context, mft_file parent, uint64_t index);

//Compares an mft number to the one in an mft record.
//Used for sorting/inserting mft records
int MFTIDCompare(const void* id, const void* mft, void* context);

#define AppendBytesFromVolRdr(context, offset, cnt, dest, pos)	\
	AppendBytesFromDiskRdr(((execution_context)(context))->dr,			\
						((offset) < 0 ? (offset) : (offset) + ((execution_context)(context))->parameters->image_offs), (cnt), (dest), (pos))

#define GetBytesFromVolRdr(context, offset, cnt)	\
	GetBytesFromDiskRdr(((execution_context)(context))->dr,			\
						((offset) < 0 ? (offset) : (offset) + ((execution_context)(context))->parameters->image_offs), (cnt))


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


bytes GetRawMFTRecord(const execution_context context, uint64_t index)
{
	index &= 0x0000FFFFFFFFFFFF;
	bytes result = NULL;
	if (index == 0)
	{
		//This is the bootstrap case, where we simply need to read record 0:
		result = GetBytesFromVolRdr(context, context->boot->logical_clust_num_for_mft *
					context->cluster_sz, context->mft_record_sz);
	}
	else
	{
		attribute file_list = FirstAttribute(context, context->mft_table, AttrTypeFlag(ATTR_DATA));
		if (file_list)
			result = GetBytesFromAttrib(context, context->mft_table, file_list, index * context->mft_record_sz, context->mft_record_sz);
	}

	if (!result)
		return ErrorCleanUp(NULL, NULL, "Unexisting record index: %lld\n", index);

	if (!DoFixUp(result, context->boot->bytes_per_sector))
	{
		return ErrorCleanUp(DeleteBytes, result, "MFT record %lld failed fix-up.\n", index);
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


mft_file LoadMFTFile(execution_context context, uint64_t index)
{
	bytes rec = GetRawMFTRecord(context, index);
	if (!rec)
		return NULL;

	SafeCreate(result, mft_file);
	
	result->mft_recs = ListOfBuffers();

	utarray_push_back(result->mft_recs, &rec);

	result->at_list = NULL;

	attribute at = FirstAttr(rec);
	for (; at; at = NextAttr(rec, at))
		if (at->type == ATTR_ATTRIBUTE_LIST)
			break;

	if (at)
	{
		// We do not use the "full-option" attribute extraction logic here. 
		// This is to avoid an endless recursion when we load MFT record 0.
		// In any case, since we do not do on-demand loading of Attribute List entries
		// (ie the pointers), we need to load the full thing any way.
		if (at->non_resident)
		{
			result->at_list = CreateEmpty();
			if (!result->at_list || !AppendBytesFromRuns(context, at, AttributeSize(at), result->at_list, 0))
				return ErrorCleanUp(DeleteMFTFile, result, "");
		}
		else
		{
			result->at_list = FromBuffer((uint8_t*)at + at->value_offs, at->value_len);
			if (!result->at_list)
				return ErrorCleanUp(DeleteMFTFile, result, "");
		}
	}

	return result;
}

attribute FirstAttribute(execution_context context, mft_file mft_rec, uint32_t attribute_mask)
{
	bytes at_rec = NULL;
	if (mft_rec->at_list)
	{
		//It's an Attribute List
		for (at_list_entry ent = FirstAttributeListEntry(mft_rec); ent; ent = NextAttributeListEntry(mft_rec, ent))
		{
			if (AttrTypeFlag(ent->type) & attribute_mask)
			{
				at_rec = RetrieveSubMFT(context, mft_rec, ent->mft_ref);
				break;
			}
		}
	}
	else if (utarray_front(mft_rec->mft_recs))
		at_rec = *(bytes*)utarray_front(mft_rec->mft_recs);

	if (!at_rec)
		return NULL;

	for (attribute at = FirstAttr(at_rec); at; at = NextAttr(at_rec, at))
		if (AttrTypeFlag(at->type) & attribute_mask)
			return at;

	return NULL;
}

attribute NextAttribute(execution_context context, mft_file mft_rec, const attribute cur, uint32_t attribute_mask)
{
	if (mft_rec->at_list)
	{
		//It's an Attribute List
		//First, locate cur and then move on from there
		at_list_entry ent = FirstAttributeListEntry(mft_rec);
		for (; ent && !IsEntryOf(ent, cur); ent = NextAttributeListEntry(mft_rec, ent));

		if (!ent)
			return NULL;

		for (ent = NextAttributeListEntry(mft_rec, ent); ent; ent = NextAttributeListEntry(mft_rec, ent))
		{
			if ((AttrTypeFlag(ent->type) & attribute_mask) && !IsEntryOf(ent,cur))
			{
				bytes at_rec = RetrieveSubMFT(context, mft_rec, ent->mft_ref);

				if (!at_rec)
					return NULL;

				for (attribute at = FirstAttr(at_rec); at; at = NextAttr(at_rec, at))
					if (IsEntryOf(ent, at))
						return at;

				return NULL;
			}
		}
		return NULL;
	}
	else
	{
		bytes *rec = utarray_front(mft_rec->mft_recs);
		if (!rec)
			return NULL;
		for (attribute at = NextAttr(*rec, cur); at; at = NextAttr(*rec, at))
			if (AttrTypeFlag(at->type) & attribute_mask)
				return at;
		return NULL;
	}
}

void DeleteMFTFile(mft_file file)
{
	if (file->at_list)
		DeleteBytes(file->at_list);

	utarray_free(file->mft_recs);

	free(file);
}

attribute_reader OpenAttributeReader(execution_context context, mft_file mft_rec, const attribute attrib)
{
	SafeCreate(result, attribute_reader);

	result->parent = mft_rec;
	result->attrib = attrib;
	result->position = 0;
	result->iter = NULL;

	SetFirstExtent(context, result);

	if (attrib->flags & ATTR_IS_COMPRESSED)
	{
		result->compr_buf = CreateEmpty();
	}
	else
		result->compr_buf = NULL;

	return result;
}

void CloseAttributeReader(attribute_reader rdr)
{
	if (rdr->iter)
		CloseRunListIterator(rdr->iter);

	if (rdr->compr_buf)
		DeleteBytes(rdr->compr_buf);

	free(rdr);
}

bytes GetBytesFromAttrib(execution_context context, mft_file mft_rec, const attribute attrib, int64_t offset, uint64_t cnt)
{
	if (offset < 0 || offset + cnt > AttributeSize(attrib))
		return NULL;

	bytes result;
	if (!attrib->non_resident)
		result = FromBuffer((uint8_t*)attrib + attrib->value_offs + offset, (rsize_t)cnt);
	else
	{
		attribute_reader rdr = OpenAttributeReader(context, mft_rec, attrib);
		if (!rdr)
			return NULL;

		result = GetBytesFromAttribRdr(context, rdr, offset, cnt);

		CloseAttributeReader(rdr);

	}
	return result;
}

bytes GetBytesFromAttribRdr(execution_context context, attribute_reader rdr, int64_t offset, uint64_t cnt)
{
	bytes result = CreateEmpty();
	if (!result)
		return NULL;

	if (!AppendBytesFromAttribRdr(context, rdr, offset, cnt, result, 0))
		return ErrorCleanUp(DeleteBytes, result, "");

	return result;
}

bool AppendBytesFromAttribRdr(execution_context context, attribute_reader rdr, int64_t offset, uint64_t cnt, bytes dest, rsize_t pos)
{
	//End of attribute reached => no more data. Do we consider this to be an error?
	//Not for the moment: up to the caller to check how many bytes we send back
	if (!rdr->extent)
		return true;

	//We trim the buffer down, which may seem strange, but it works, because the underlying file-reader
	//will extend it for us. And the size of the buffer will help us keep track of the number of bytes
	//we've read already:
	RightTrim(dest, dest->buffer_len - pos);


	//Check we're not reading outside of the attribute dimensions:
	//we also need to make sure that in case of a non-resident attribute
	//we never read more than attrib->init_sz:
	uint64_t start_pos = offset >= 0 ? offset : rdr->position;
	if (start_pos >= AttributeSize(rdr->attrib))
		return true;
	uint64_t read_cnt = cnt = min(cnt, AttributeSize(rdr->attrib) - start_pos);
	
	if (rdr->attrib->non_resident && rdr->attrib->init_sz != rdr->attrib->real_sz)
		read_cnt = max(0, (int64_t)rdr->attrib->init_sz - start_pos);

	bool result = true;
	if (read_cnt > 0)
	{
		//There are freak cases out there, with compressed flag set, which are 
		//resident => that shouldn't happen
		if ((rdr->attrib->flags & ATTR_IS_COMPRESSED) && rdr->attrib->non_resident)
			result = AppendCompressedBytesFromAttribRdr(context, rdr, offset, read_cnt, dest, pos);
		else
			result = AppendNormalBytesFromAttribRdr(context, rdr, offset, read_cnt, dest, pos);
	}
	result = result && (dest->buffer_len == pos + read_cnt);
	if (result && read_cnt < cnt)
		SetBytes(dest, 0, dest->buffer_len, (rsize_t)(cnt - read_cnt));

	return result;
}

bool AppendNormalBytesFromAttribRdr(execution_context context, attribute_reader rdr, int64_t offset, uint64_t cnt, bytes dest, rsize_t pos)
{
	if (offset >= 0)
	{
		//First find the extent that contains the offset:

		for (SetFirstExtent(context, rdr); rdr->extent && context->cluster_sz * (rdr->extent->end_vcn + 1) < (uint64_t)offset;
								SetNextExtent(context, rdr));

		//Next, set the position, and, in case of a non resident attribute,
		//position the run iterator on the correct run list entry
		rdr->position = offset;
		if (rdr->extent)
		{
			if (rdr->extent->non_resident)
				for (; !rdr->iter->end_of_runs && context->cluster_sz * rdr->iter->next_vcn < (uint64_t)offset; NextRun(rdr->iter));
		}
	}
	else
		offset = rdr->position;

	for (; rdr->extent; SetNextExtent(context, rdr))
	{
		//Read bytes from current extent until we have the bytes we need or no more bytes left in extent
		if (rdr->extent->non_resident)
		{
			for (; !rdr->iter->end_of_runs; NextRun(rdr->iter))
			{
				uint64_t rl_offset = max(0,  offset - (int64_t)rdr->iter->cur_vcn*context->cluster_sz);
				uint64_t delta = min((rdr->iter->next_vcn - rdr->iter->cur_vcn) * context->cluster_sz - rl_offset, pos + cnt - dest->buffer_len);
				if (rdr->iter->cur_lcn)
					AppendBytesFromVolRdr(context, rdr->iter->cur_lcn * context->cluster_sz + rl_offset, delta, dest, dest->buffer_len);
				else
					SetBytes(dest, 0, dest->buffer_len, (rsize_t)delta);

				rdr->position += delta;
				if (dest->buffer_len >= pos + cnt)
					break;
			}
		}
		else
		{
			uint64_t delta = min(pos + cnt - dest->buffer_len, rdr->extent->value_len - rdr->position);
			Reserve(dest, dest->buffer_len + (rsize_t)delta);
			memcpy(dest->buffer + dest->buffer_len - delta, (char*)rdr->extent + rdr->extent->value_offs + rdr->position, (rsize_t)delta);
			rdr->position += delta;
		}
		if (dest->buffer_len >= pos + cnt)
			break;
	}
	//For the moment still no error handling, all deviant cases should be caught in underlying
	//functions and simply result in zero or not enough bytes being returned
	return true;
}

bool AppendCompressedBytesFromAttribRdr(execution_context context, attribute_reader rdr, int64_t offset, uint64_t cnt, bytes dest, rsize_t pos)
{
	uint64_t block_sz = (1ULL << rdr->attrib->compr_unit);
	uint64_t block_sz_bt = block_sz *context->cluster_sz;

	if (offset == 0)
		SetFirstExtent(context, rdr);
	else if (offset > 0)
	{
		uint64_t start_cl = (offset / block_sz_bt) * block_sz;

		//First find the extent that contains the start of the buffer in which offset is
		for (SetFirstExtent(context, rdr); rdr->extent && rdr->extent->end_vcn < start_cl; SetNextExtent(context, rdr));
		if (!rdr->extent)
			return false;

		//Next, move the iterator forward until we reach the mapping pair that contains offset
		for (; !rdr->iter->end_of_runs && rdr->iter->next_vcn <= start_cl; NextRun(rdr->iter));
		if (rdr->iter->end_of_runs)
			return false;

		//At this point, offset is contained in the current mapping pair of rdr->iter
		//We now need to update the state of the reader to ensure consistency between
		//rdr->position and offset AND make sure that rdr->position is block-aligned

		if (offset % block_sz_bt)
		{
			//It's not block-aligned
			//Set position to start of block before offset...
			rdr->position = context->cluster_sz * start_cl;
			RightTrim(rdr->compr_buf, rdr->compr_buf->buffer_len);

			//and get that block from a recursive call:
			AppendCompressedBytesFromAttribRdr(context, rdr, -1, block_sz_bt, rdr->compr_buf, 0);
			uint64_t delta = min(rdr->position - offset, cnt);
			uint64_t buf_end = min(block_sz_bt, cnt);

			Append(dest, rdr->compr_buf, (rsize_t)(offset % block_sz_bt), (rsize_t)delta);

			if (offset + cnt <= rdr->position)
			{
				//cnt was small enough to stay in the same block, we're done here:
				rdr->position = offset + cnt;
				return true;
			}
		}
		else
			rdr->position = offset;
	}
	else //offset < 0, the continuation case
	{
		offset = rdr->position;
		//The "normal" continuation case would be to have an empty buffer, with 'position' set at the start
		//of a new block, which would indicate we've handled a complete one last time.
		//In that case, no action would be needed, and we could go straight into the main loop
		if ((rdr->compr_buf->buffer_len != 0 || offset % block_sz_bt != 0))
		{
			uint64_t skip = offset % block_sz_bt;
			uint64_t delta = min(block_sz_bt - skip, cnt);
			if (rdr->compr_buf->buffer_len == 0 && skip != 0)
				//We're not in that case however, this means we have an incomplete sparse (because buffer is empty)
				//block to handle
				SetBytes(dest, 0, dest->buffer_len, (rsize_t)delta);
			else if (rdr->compr_buf->buffer_len == block_sz_bt)
				//State of reader is OK from last time, just need to handle what was in the buffer
				AppendAt(dest, pos, rdr->compr_buf, (rsize_t)skip, (rsize_t)delta);
			else if (rdr->compr_buf->buffer_len != 0)
				return CleanUpAndFail(NULL, NULL, "Inconsistent state of attribute reader.\n");

			if (skip + delta > 0 && skip + delta < block_sz_bt)
			{
				//cnt was small enough to stay in the same block, we're done here:
				rdr->position = offset + cnt;
				return true;
			}
			rdr->position = offset + delta;
		}
	}

	uint64_t block_st = dest->buffer_len;
	uint64_t empty_cnt = 0;
	//uint64_t end_pos = ((offset + cnt + block_sz_bt - 1) / block_sz_bt) * block_sz_bt;
	uint64_t end_cl = ((offset + cnt + block_sz_bt - 1) / block_sz_bt) * block_sz;
	bytes compr_bf = CreateEmpty();

	for (; rdr->extent; SetNextExtent(context, rdr))
	{
		for (; !rdr->iter->end_of_runs; NextRun(rdr->iter))
		{
			if (rdr->iter->cur_lcn != 0)
			{
				//Real clusters, can either be part of a compressed or non-compressed block;
				//Simply append them to dest:
				uint64_t clust_cnt = min(end_cl, rdr->iter->next_vcn) - rdr->iter->cur_vcn;
				uint64_t skip_cl = max(rdr->position / context->cluster_sz, rdr->iter->cur_vcn) - rdr->iter->cur_vcn;

				AppendBytesFromVolRdr(context, context->cluster_sz*(rdr->iter->cur_lcn + skip_cl),
							context->cluster_sz * (clust_cnt - skip_cl), dest, dest->buffer_len);

				//All full-sized blocks between block_st and end of dest are normal, we can simply
				//commit these, by advancing block_st

				block_st = dest->buffer_len - (dest->buffer_len - block_st) % block_sz_bt;
				rdr->position += context->cluster_sz * (clust_cnt - skip_cl);

				//If we have sparse buffers pending, that would result in an inconsistency,
				//therefore we should flush them, maybe add error handling later on:
				empty_cnt = 0;
			}
			else
			{
				uint64_t extra_clust = min(end_cl, rdr->iter->next_vcn) - max(rdr->iter->cur_vcn, rdr->position / context->cluster_sz);
				empty_cnt += extra_clust;
				rdr->position += context->cluster_sz * extra_clust;

				uint64_t cl_len = (dest->buffer_len - block_st) / context->cluster_sz;
				if (cl_len > 0ULL)
				{
					//Compressed block:
					if (cl_len + empty_cnt >= block_sz)
					{
						Reserve(compr_bf, (rsize_t)block_sz_bt);
						//Decompress the block starting at block_sz into the buffer:
						if (!LZNT1Decompress(dest, (rsize_t)block_st, compr_bf, 0))
							return CleanUpAndFail(DeleteBytes, compr_bf, "Decompression error.\n");

						//Copy the result back into dest:
						AppendAt(dest, (rsize_t)block_st, compr_bf, 0, (rsize_t)block_sz_bt);
						block_st += block_sz_bt;
						empty_cnt -= (block_sz - cl_len);
					}
				}
				if (empty_cnt >= block_sz)
				{
					uint64_t delta = context->cluster_sz * empty_cnt;
					SetBytes(dest, 0, dest->buffer_len, (rsize_t)delta);
					block_st += delta;
					empty_cnt -= empty_cnt;
				}
			}

			if (rdr->position >= end_cl * context->cluster_sz)
				break;
		}

		if (rdr->position >= end_cl * context->cluster_sz)
			break;
	}

	if ((offset + cnt) % block_sz_bt != 0)
	{
		RightTrim(rdr->compr_buf, rdr->compr_buf->buffer_len);
		//Keep the last block for a  potential next read:
		AppendAt(rdr->compr_buf, 0, dest, dest->buffer_len - (rsize_t)block_sz_bt, (rsize_t)block_sz_bt);
		RightTrim(dest, (rsize_t)(block_sz_bt - (offset + cnt) % block_sz_bt));
		rdr->position = offset + cnt;
	}
	DeleteBytes(compr_bf);

	return true;
}

bool AppendBytesFromRuns(execution_context context, const attribute attr, uint64_t cnt, bytes dest, uint64_t pos)
{
	if (!attr->non_resident)
		return false;
	Reserve(dest, (rsize_t)(pos + cnt));
	run_list_iterator iter = StartRunListIterator(attr);
	for (; !iter->end_of_runs && pos < dest->buffer_len; NextRun(iter))
	{
		uint64_t extra = min(context->cluster_sz * (iter->next_vcn - iter->cur_vcn), dest->buffer_len - pos);
		if (iter->cur_lcn == 0ULL)
			SetBytes(dest, 0, (rsize_t)pos, (rsize_t)extra);
		else
			AppendBytesFromVolRdr(context, context->cluster_sz * iter->cur_lcn, (rsize_t)extra, dest, pos);
		pos += extra;
	}
	RightTrim(dest, dest->buffer_len - (rsize_t)pos);
	CloseRunListIterator(iter);
	return true;
}

void SetFirstExtent(execution_context context, attribute_reader rdr)
{
	rdr->extent = NULL;
	rdr->entry = NULL;
	if (!rdr->parent->at_list)
	{
		//No Attribute List => there is only one extent, ie the main attribute
		rdr->extent = rdr->attrib;
		rdr->entry = NULL;
	}

	for (at_list_entry ent = FirstAttributeListEntry(rdr->parent); !rdr->extent && ent; ent = NextAttributeListEntry(rdr->parent, ent))
	{
		//First locate the entry that contains the extent
		//Since we need the first one and the entries are sorted, we can stop as soon
		//as we find a matching id
		if (IsEntryOf(ent, rdr->attrib))
		{
			rdr->mft_sub_rec = RetrieveSubMFT(context, rdr->parent, ent->mft_ref);
			if (!rdr->mft_sub_rec)
				return;
			rdr->entry = ent;

			//Now that we have the record, find the extent that matches the entry,
			//this must be the first one that has the same id:
			for (rdr->extent = FirstAttr(rdr->mft_sub_rec); rdr->extent && !IsExtentOf(rdr->extent, rdr->attrib);
				rdr->extent = NextAttr(rdr->mft_sub_rec, rdr->extent));
		}
	}

	if (!rdr->extent)
		return;

	if (rdr->extent->non_resident)
	{
		if (rdr->iter)
			CloseRunListIterator(rdr->iter);
		rdr->iter = StartRunListIterator(rdr->extent);
	}
	else
		rdr->iter = NULL;
}

void SetNextExtent(execution_context context, attribute_reader rdr)
{
	if (!rdr->parent->at_list)
	{
		//No Attribute List => there is only one extent, ie the main attribute
		rdr->extent = NULL;
		return;
	}

	rdr->entry = NextAttributeListEntry(rdr->parent, rdr->entry);
	if (!rdr->entry)
		return;

	rdr->extent = NextAttr(rdr->mft_sub_rec, rdr->extent);
	if (!rdr->extent || !IsExtentOf(rdr->extent, rdr->attrib))
	{
		rdr->mft_sub_rec = RetrieveSubMFT(context, rdr->parent, rdr->entry->mft_ref);
		if (!rdr->mft_sub_rec)
			return;

		//Since attribute extents are sorted, the first one must be ours:
		rdr->extent = FirstAttr(rdr->mft_sub_rec);
		if (!IsExtentOf(rdr->extent, rdr->attrib))
		{
			rdr->extent = NULL;
			return;
		}
	}

	if (rdr->extent->non_resident)
	{
		if (rdr->iter)
			CloseRunListIterator(rdr->iter);
		rdr->iter = StartRunListIterator(rdr->extent);
	}
	else
		rdr->iter = NULL;
}

attribute FirstAttr(bytes mft_rec)
{
	attribute attr = (attribute)(mft_rec->buffer + ((raw_mft_record)(mft_rec->buffer))->attrs_offs);
	if (attr->type == ATTR_ATTRIBUTE_END_MARKER)
		return NULL;
	else
		return attr;
}

attribute NextAttr(bytes mft_rec, const attribute cur)
{
	attribute result = (attribute)((char*)cur + cur->length);
	if (result->type == ATTR_ATTRIBUTE_END_MARKER)
		return NULL;
	else
		return result;
}


bytes RetrieveSubMFT(execution_context context, mft_file parent, uint64_t index)
{
	int32_t rec_ind = FindInArray(parent->mft_recs, &index, NULL, MFTIDCompare);
	if (rec_ind < 0)
	{
		bytes new_rec = GetRawMFTRecord(context, index);
		if (!new_rec)
			return NULL;
		rec_ind = ~rec_ind;
		utarray_insert(parent->mft_recs, &new_rec, (size_t)rec_ind);

		return new_rec;
	}
	bytes *result_ptr = (bytes*)utarray_eltptr(parent->mft_recs, (size_t)rec_ind);
	return result_ptr ? *result_ptr : NULL;
}

int MFTIDCompare(const void* id, const void* mft, void *context)
{
	uint64_t first = *(uint64_t *)id & 0x0000FFFFFFFFFFFF;
	uint64_t second = ((raw_mft_record)((*(bytes *)mft)->buffer))->mft_rec_number & 0x0000FFFFFFFFFFFF;
	return first > second ? 1 : (first < second ? - 1 : 0);
}
