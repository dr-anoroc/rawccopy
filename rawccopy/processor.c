
#include <stdlib.h>

#include "attribs.h"
#include "datarun.h"
#include "processor.h"
#include "mft.h"
#include "index.h"
#include "path.h"


#define TO_CLUST(context,bt_cnt) ((bt_cnt) / BytesPerCluster(context->boot))
#define TO_BYTES(context,cl_cnt) ((uint64_t)(cl_cnt) * BytesPerCluster(context->boot))

bool ExractTargetFile(execution_context context, const wchar_t* file_name, uint64_t mft_ref);

bool ExtractDataAttributes(execution_context context, const wchar_t* file_name, const mft_record source);

bool ExtractOtherAttributes(execution_context context, const wchar_t* file_name, const mft_record source, uint64_t mft_ref);

bool ExtractDataStream(execution_context context, const wchar_t* stream_name, const UT_array* attribs,
							const bytes start, const bytes end);

wchar_t* GenerateStreamName(const attribute at, const wchar_t* file_name);

bool WriteSparseClusters(execution_context context, const wchar_t* file_name, uint64_t count,
					uint64_t max_bytes, uint32_t block_sz);

bool ExtractCompressedAttribute(execution_context context, const wchar_t* file_name, const attribute attr, const UT_array* runs);

bool WriteZeros(execution_context context, const wchar_t* file_name, uint64_t count);

bytes ZeroBuffer(uint32_t count);

void WritePathInfo(const execution_context context, const resolved_path res_path);

bool PerformOperation(execution_context context)
{
	wchar_t* base_file = NULL;
	if (context->parameters->output_file && *context->parameters->output_file)
		base_file = DuplicateString(context->parameters->output_file);

	bool result;
	if (!context->parameters->mft_ref)
	{
		resolved_path res_path;
		result = TryParsePath(context, context->parameters->target_path, &res_path);

		if (context->parameters->detail_mode > 0)
			WritePathInfo(context, res_path);

		path_step hit;
		if (result && (hit = utarray_back(res_path)))
		{
			if (!base_file)
				base_file = FileNameFromIndex(IndexEntryPtr(hit->original));
			result = ExractTargetFile(context, base_file, IndexEntryPtr(DerefStep(hit))->mft_reference);
		}
		if (res_path)
			DeletePath(res_path);
	}
	else
	{
		result = ExractTargetFile(context, base_file, *context->parameters->mft_ref);
	}
	if (base_file)
		free(base_file);
	return result;
}

bool ExractTargetFile(execution_context context, const wchar_t *file_name, uint64_t mft_ref)
{
	uint32_t attr_filter = context->parameters->all_attribs ? 0xFFFFF : AttrTypeFlag(ATTR_DATA);
	if (!file_name)
		attr_filter |= AttrTypeFlag(ATTR_FILE_NAME);

	mft_record rec = AttributesForFile(context, mft_ref, attr_filter);
	if (!rec)
		return false;

	wchar_t* local_fn = (wchar_t *)file_name;
	if (!local_fn || !*local_fn)
		local_fn = FileNameFromMFTRec(rec);

	bool result = ExtractDataAttributes(context, local_fn, rec);

	if (result && context->parameters->all_attribs)
		result = ExtractOtherAttributes(context, local_fn, rec, mft_ref);

	if (local_fn != file_name)
		free(local_fn);

	DeleteMFTRecord(rec);

	return result;
}

wchar_t* GenerateStreamName(const attribute at, const wchar_t *file_name)
{
	wchar_t* at_name = GetAttributeName(at);
	StringAssign(
		at_name,
		*at_name ? JoinStrings(L"", file_name, L"[ADS_", at_name, L"]", NULL) : DuplicateString(file_name));

	return at_name;
}

bool ExtractDataAttributes(execution_context context, const wchar_t *file_name, const mft_record source)
{
	// Data attributes come in sequences and represent streams
	// Each file has at most one unnamed stream and any number of named streams
	// There are some typical cases on how this look like in reality:
	// * a sequence of [unnamed] [unnamed] ... [unnamed]
	// * a sequence of [unnamed] [unnamed] ... [unnamed] [named]
	// * a sequence of [named] [unnamed] [unnamed] [unnamed]
	// If we treat 'unnamed' as an empty name, the list of attributes looks like:
	// {name1: [] [] []} {name2: [] [] []} ... {nameN: [] [] []}
	// The above representation makes the important undocumented assumption
	// that there is no interleaving of attributes, ie a sequence like
	// [unnamed] [unnamed] [name1] [unnamed] [name1] can never occur.
	// We will also use that assumption, so that means that we go through
	// the attributes, group them together by name and read/write out one
	// stream for each name.

	UT_array* attribs = GetAttributes(ATTR_DATA, source);

	bytes start = utarray_front(attribs);
	if (!start)
		return true;

	wchar_t* at_name = GetAttributeName(AttributePtr(start, 0));
	bytes cur = start;
	wchar_t* cur_name = NULL;
	bool result = true;
	do
	{
		cur = utarray_next(attribs, cur);
		if (cur)	
			StringAssign(cur_name, GetAttributeName(AttributePtr(cur, 0)));

		if (!cur || wstrncmp_nocase(at_name, cur_name, 500))
		{
			wchar_t *stream_name = GenerateStreamName(AttributePtr(start,0), file_name);
			result = ExtractDataStream(context, stream_name, attribs, start, cur);
			free(stream_name);
			if (!result || !cur)
				break;
			StringAssign(at_name, cur_name);
			start = cur;
		}
	} while (cur);

	if (at_name && at_name != cur_name)
		free(at_name);
	if (cur_name)
		free(cur_name);

	return result;
}

bool ExtractOtherAttributes(execution_context context, const wchar_t *file_name, const mft_record source, uint64_t mft_ref)
{
	mft_ref &= 0x000000FFFFFFFFFF;
	bool result = true;
	for (uint32_t i = 0; i < 0x10 && result; ++i)
	{
		uint32_t type = IndexToAttrType(i);
		if (type == ATTR_DATA)
			continue;

		UT_array* at_lst = GetAttributes(type, source);
		if (!at_lst || utarray_len(at_lst) == 0)
			continue;

		wchar_t* stream_name = NULL;
		wchar_t* file_path = NULL;

		for (bytes at = utarray_front(at_lst); at && result; at = utarray_next(at_lst,at))
		{
			attribute attr = AttributePtr(at, 0);
			StringAssign(stream_name, GetAttributeName(attr));

			StringAssign(file_path, DuplicateString((wchar_t*)file_name));
			wchar_t* num_ref = NumberToString(mft_ref, 10);
			wchar_t* num_indx = utarray_len(at_lst) > 1 ? NumberToString(utarray_eltidx(at_lst, at) + 1LL, 10) : L"";
			StringAssign(file_path, JoinStrings(L"_", file_path, num_ref, AttributeTypeName(type), stream_name, 
				num_indx, NULL));
			free(num_ref);
			if (*num_indx)
				free(num_indx);
			StringAssign(file_path, JoinStrings(L"", file_path, L".bin", NULL));

			bytes content = GetAttributeData(context->cr, attr);
			result = WriteDataToDestination(context, file_path, content);
			DeleteBytes(content);
		}
		free(stream_name);
		free(file_path);
	}
	return result;
}


bool ExtractDataStream(execution_context context, const wchar_t* stream_name, const UT_array* attribs,
	const bytes start, const bytes end)
{
	attribute start_at = AttributePtr(start, 0);
	bool result = true;
	if (!start_at->non_resident)
	{
		bytes payload = GetAttributeData(context->cr, start_at);
		result = WriteDataToDestination(context, stream_name, payload);
		DeleteBytes(payload);
		return result;
	}

	bytes cur = start;
	UT_array* runs = EmptyRunList();
	uint64_t TotClusters = 0;
	for (; cur != end && TotClusters < TO_CLUST(context, start_at->real_sz); cur = utarray_next(attribs, cur))
	{
		attribute attr = AttributePtr(cur, 0);
		TotClusters += attr->end_vcn - attr->start_vcn + 1;
		ExtractRunsFromAttribute(runs, attr);
	}

	if (TotClusters < TO_CLUST(context, start_at->real_sz))
	{
		utarray_free(runs);
		wprintf(L"Encountered incomplete attribute.\n");
		return false;
	}

	if (start_at->flags & ATTR_IS_COMPRESSED)
	{
		result = ExtractCompressedAttribute(context, stream_name, start_at, runs);
	}
	else //Could also test for ATTR_IS_SPARSE, but it adds little value except for identifying
		 //errors we can't handle anyway
	{
		uint64_t byte_cnt = start_at->init_sz;

		//We use blocks of 16 clusters, but the only purpose is to slice the reading/writing into units
		bytes rd_buf = CreateBytes((rsize_t)TO_BYTES(context, 16));
		for (run* scr = (run*)utarray_front(runs); scr && byte_cnt > 0 && result; scr = utarray_next(runs, scr))
		{
			if (scr->vcn == 0)
			{
				result = WriteSparseClusters(context, stream_name, scr->clusters, byte_cnt, 16);
				//byte_cnt -= min(16ULL*TO_BYTES(context, (uint64_t)scr->Clusters), byte_cnt);
				byte_cnt -= min(16ULL*TO_BYTES(context, scr->clusters), byte_cnt);
			}
			else
			{
				SetNextCluster(context->cr, scr->vcn);
				for (uint32_t run_cl = scr->clusters; result && run_cl > 0 && byte_cnt > 0;)
				{
					uint32_t buf_sz = min(run_cl, 16);
					ReadNextClustersIn(context->cr, buf_sz, rd_buf);
					RightTrim(rd_buf, rd_buf->buffer_len - min((rsize_t)byte_cnt, TO_BYTES(context, buf_sz)));
					result = WriteDataToDestination(context, stream_name, rd_buf);
					run_cl -= buf_sz;
					byte_cnt -= rd_buf->buffer_len;
				}
			}
		}
		DeleteBytes(rd_buf);
	}

	utarray_free(runs);
	//This was in the original version, not sure if it's needed / relevant?
	if (start_at->real_sz > start_at->init_sz)
		result = WriteZeros(context, stream_name, start_at->real_sz - start_at->init_sz);

	return result;
}

bool ExtractCompressedAttribute(execution_context context, const wchar_t* file_name, const attribute attr, const UT_array* runs)
{
	uint64_t byte_cnt = attr->init_sz;

	uint32_t block_sz = 1L << attr->compr_unit;
	bytes dec_buf = CreateBytes((rsize_t)TO_BYTES(context,block_sz));
	bytes rd_buf = CreateEmpty();
	bytes sprs_buf = ZeroBuffer((rsize_t)TO_BYTES(context, block_sz));
	uint32_t  empty_cnt = 0;
	bool result = true;
	for (run *cur = utarray_front(runs); cur && byte_cnt > 0 && result; cur = utarray_next(runs, cur))
	{
		//At this stage we have a cluster queue that contains less than 'block_sz' clusters
		if (cur->vcn != 0)
		{
			//We have extra clusters with non-zero VCNs, if they make up more than one block, they're normal:
			//write them out, otherwise read them and leave them in rd_buf
			bytes extra = ReadClusters(context->cr, cur->vcn,
				min(block_sz - TO_CLUST(context, (uint32_t)rd_buf->buffer_len), cur->clusters));
			Append(rd_buf, extra, 0, extra->buffer_len);
			DeleteBytes(extra);
			uint32_t clust_cnt = cur->clusters;
			uint32_t buf_sz = 0;
			for (buf_sz = TO_CLUST(context, (uint32_t)rd_buf->buffer_len); buf_sz == block_sz && result; )
			{
				result = WriteDataToDestination(context, file_name, rd_buf);
				byte_cnt -= rd_buf->buffer_len;
				clust_cnt -= block_sz;
				buf_sz = min(block_sz, clust_cnt);
				ReadNextClustersIn(context->cr, buf_sz, rd_buf);
			}
			//At this point, all runs with non-zero VCNs, have been read
			//All of it made up of full blocks is written away. The rest (< full block)
			//is in rd_buf
			RightTrim(rd_buf, rd_buf->buffer_len - TO_BYTES(context, buf_sz));
		}
		else
		{
			empty_cnt += cur->clusters;
			if (rd_buf->buffer_len > 0)
			{
				//Compressed block
				uint32_t cmp_sz = TO_CLUST(context, (uint32_t)rd_buf->buffer_len);
				if (cmp_sz + empty_cnt >= block_sz)
				{
					result = LZNT1Decompress(rd_buf, dec_buf);
					if (!result)
						printf("Decompression error\n");
					else
					{
						if (dec_buf->buffer_len > byte_cnt)
							RightTrim(dec_buf, (int32_t)(dec_buf->buffer_len - byte_cnt));
						result = WriteDataToDestination(context, file_name, dec_buf);
					}

					byte_cnt -= dec_buf->buffer_len;
					empty_cnt -= (block_sz - cmp_sz);
					RightTrim(rd_buf, rd_buf->buffer_len);
				}
			}
			result = WriteSparseClusters(context, file_name, empty_cnt, byte_cnt, block_sz);
			byte_cnt -= min(TO_BYTES(context, (uint64_t)empty_cnt), byte_cnt);
			empty_cnt = 0;
		}		

	}
	DeleteBytes(sprs_buf);
	DeleteBytes(dec_buf);
	DeleteBytes(rd_buf);
	return result;
}

bool WriteSparseClusters(execution_context context, const wchar_t* file_name, uint64_t count,
	uint64_t max_bytes, uint32_t block_sz)
{
	uint64_t byte_cnt = 0;
	bytes sprs_buf = ZeroBuffer(TO_BYTES(context,block_sz));
	bool result = true;
	for (; count > 0 && byte_cnt <= max_bytes && result;)
	{
		uint64_t buf_sz = min(count, block_sz);
		RightTrim(sprs_buf, sprs_buf->buffer_len - min((rsize_t)(max_bytes - byte_cnt), (rsize_t)TO_BYTES(context, buf_sz)));
		result = WriteDataToDestination(context, file_name, sprs_buf);
		count -= buf_sz;
		byte_cnt += sprs_buf->buffer_len;
	}
	DeleteBytes(sprs_buf);
	return result;
}

bool WriteZeros(execution_context context, const wchar_t* file_name, uint64_t count)
{
	bytes buf;
	if ((buf = ZeroBuffer((uint32_t)min(MAXUINT32,count))) == NULL)
		return false;
	bool result = true;

	while (count > 0 && result)
	{
		result = WriteDataToDestination(context, file_name, buf);
		count -= buf->buffer_len;
		RightTrim(buf, buf->buffer_len - (rsize_t)min(count, MAXUINT32));
	}
	DeleteBytes(buf);
	return result;
}

bytes ZeroBuffer(uint32_t count)
{
	SafeCreate(result, bytes);
	uint8_t* buf = calloc(count, 1);
	if (!buf)
	{
		free(result);
		printf("Memory allocation problem.\n");
		exit(-1);
		return false;;
	}
	result->buffer = buf;
	result->buffer_len = count;
	return result;
}

void WritePathInfo(const execution_context context, const resolved_path res_path)
{
	path_step last_folder = utarray_back(res_path);
	if (!last_folder)
		return;

	if (!(IndexEntryPtr(DerefStep(last_folder))->file_flags & FILE_ATTR_DUP_FILE_NAME_INDEX_PRESENT))
	{
		//It's a file, go one step up...
		last_folder = utarray_prev(res_path, last_folder);
		if (!last_folder)
			return;
	}

	wchar_t* file_st = wcschr(context->parameters->target_path, L'\\');

	wprintf(L"Directory listing for: %.*ls", (int)(file_st - context->parameters->target_path),
								context->parameters->target_path);

	for (path_step cur = utarray_eltptr(res_path, 1); cur; cur = utarray_next(res_path, cur))
	{
		index_entry ent = IndexEntryPtr(cur->original);
		wprintf(L"\\%.*ls", (int)ent->filename_len, (wchar_t*)ent->filename);
		if (cur == last_folder)
			break;
	}
	wprintf(L"\n\n");

	index_iter iter = StartIndexIterator(context, IndexEntryPtr(DerefStep(last_folder)));
	if (!iter)
		return;

	wchar_t* buffer = NULL;

	if (context->parameters->detail_mode == 1)
	{
		for (index_entry rec = CurrentIterEntry(iter); rec; rec = NextIterEntry(iter))
		{
			StringAssign(buffer, FileNameFromIndex(rec));
			wprintf(L"FileName: %ls\n", buffer);
			wprintf(L"MFT Ref: %lld\n", rec->mft_reference & 0x000000FFFFFFFFFF);
			wprintf(L"MFT Ref SeqNo: %lld\n", rec->mft_reference >> 40);
			wprintf(L"Parent MFT Ref: %lld\n", rec->parent_mft_ref & 0x000000FFFFFFFFFF);
			wprintf(L"Parent MFT Ref SeqNo: %lld\n", rec->parent_mft_ref >> 40);
			StringAssign(buffer, FileFlagsFromIndexRec(rec));
			wprintf(L"Flags: %ls\n", buffer);
			StringAssign(buffer, FormatNTFSDate(rec->creation_tm));
			wprintf(L"File Create Time: %ls\n", buffer);
			StringAssign(buffer, FormatNTFSDate(rec->file_last_modif_tm));
			wprintf(L"File Modified Time: %ls\n", buffer);
			StringAssign(buffer, FormatNTFSDate(rec->last_modif_tm));
			wprintf(L"MFT Entry modified Time: %ls\n", buffer);
			StringAssign(buffer, FormatNTFSDate(rec->last_access_tm));
			wprintf(L"File Last Access Time: %ls\n", buffer);
			wprintf(L"Allocated Size: %llu\n", rec->allocated_sz);
			wprintf(L"Real Size: %llu\n", rec->real_sz);
			wprintf(L"NameSpace: %ls\n", NameSpaceLabel(rec->namespace));
			if (rec->index_flags == 1)
			{
				wprintf(L"Flags: Index Entry node\n");
				wprintf(L"SubNodeVCN: %llu\n", SubNodeEntry(rec));
			}
			else
			{
				wprintf(L"Flags:\n");
				wprintf(L"SubNodeVCN:\n");
			}
			wprintf(L"\n");
		}
	}
	else
	{
		wprintf(L"  File Modified Time |  Type | %22.22ls | FileName\n", L" ");
		for (index_entry rec = CurrentIterEntry(iter); rec; rec = NextIterEntry(iter))
		{
			StringAssign(buffer, FormatNTFSDate(rec->file_last_modif_tm));
			wprintf(L"%ls | %5.5ls | %22.llu |", buffer,
				rec->file_flags & FILE_ATTR_DUP_FILE_NAME_INDEX_PRESENT ? L"<DIR>" : L" ", rec->real_sz);
			StringAssign(buffer, FileNameFromIndex(rec));
			wprintf(L" %ls\n", buffer);
		}
	}
	free(buffer);
	CloseIndexIterator(iter);
}

