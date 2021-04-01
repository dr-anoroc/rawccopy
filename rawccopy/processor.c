
#include <stdlib.h>

#include "safe-string.h"
#include "attribs.h"
#include "processor.h"
#include "mft.h"
#include "index.h"
#include "path.h"


void WritePathInfo(execution_context context, const resolved_path res_path);

bool ExtractAttributes(execution_context context, const string file_name, uint64_t mft_ref);

bool WriteAttributeContent(execution_context context, mft_file file, attribute at, const string file_name);

string PreferedFileName(execution_context context, mft_file file);

bool PrintNTFSDate(uint64_t date, string str, rsize_t offset);

bool PerformOperation(execution_context context)
{
	bool result;
	if (!context->parameters->mft_ref)
	{
		resolved_path res_path;
		result = TryParsePath(context, BaseString(context->parameters->source_path), &res_path);

		if (context->parameters->detail_mode > 0)
			WritePathInfo(context, res_path);

		path_step hit;
		if (result && (hit = utarray_back(res_path)))
		{
			if (context->parameters->output_file && StringLen(context->parameters->output_file) > 0)
				result = ExtractAttributes(context, context->parameters->output_file, IndexEntryPtr(DerefStep(hit))->mft_reference);
			else
			{
				string base_file = StringPrint(NULL, 0, L"%.*ls", IndexEntryPtr(hit->original)->filename_len, 
									(wchar_t*)IndexEntryPtr(hit->original)->filename);
				result = ExtractAttributes(context, base_file, IndexEntryPtr(DerefStep(hit))->mft_reference);
				DeleteString(base_file);
			}
			
		}
		if (res_path)
			DeletePath(res_path);
	}
	else
	{
		result = ExtractAttributes(context, context->parameters->output_file, *context->parameters->mft_ref);
	}

	return result;
}

void WritePathInfo(execution_context context, const resolved_path res_path)
{
	path_step last_folder = utarray_back(res_path);
	if (!last_folder)
		return;

	if (!(IndexEntryPtr(DerefStep(last_folder))->file_flags & FILE_ATTR_DUP_FILE_NAME_INDEX_PRESENT))
		return;

	rsize_t file_st = wcschr(BaseString(context->parameters->source_path), L'\\') - BaseString(context->parameters->source_path);

	wprintf(L"Directory listing for: %.*ls", (int)file_st, BaseString(context->parameters->source_path));

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

	string str_buf = NewString();
	if (context->parameters->detail_mode == 1)
	{
		for (index_entry rec = CurrentIterEntry(iter); rec; rec = NextIterEntry(context, iter))
		{
			wprintf(L"FileName: %.*ls\n", rec->filename_len, (wchar_t*)(rec->filename));
			wprintf(L"MFT Ref: %lld\n", rec->mft_reference & 0x000000FFFFFFFFFF);
			wprintf(L"MFT Ref SeqNo: %lld\n", rec->mft_reference >> 40);
			wprintf(L"Parent MFT Ref: %lld\n", rec->parent_mft_ref & 0x000000FFFFFFFFFF);
			wprintf(L"Parent MFT Ref SeqNo: %lld\n", rec->parent_mft_ref >> 40);
			FileFlagsFromIndexRec(rec, str_buf);
			wprintf(L"Flags: %ls\n", BaseString(str_buf));

			PrintNTFSDate(rec->creation_tm, str_buf, 0);
			wprintf(L"File Create Time: %ls\n", BaseString(str_buf));
			PrintNTFSDate(rec->file_last_modif_tm, str_buf, 0);
			wprintf(L"File Modified Time: %ls\n", BaseString(str_buf));
			PrintNTFSDate(rec->last_modif_tm, str_buf, 0);
			wprintf(L"MFT Entry modified Time: %ls\n", BaseString(str_buf));
			PrintNTFSDate(rec->last_access_tm, str_buf, 0);
			wprintf(L"File Last Access Time: %ls\n", BaseString(str_buf));
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
		for (index_entry rec = CurrentIterEntry(iter); rec; rec = NextIterEntry(context, iter))
		{
			PrintNTFSDate(rec->file_last_modif_tm, str_buf, 0);
			wprintf(L"%ls | %5.5ls | %22.llu |", BaseString(str_buf),
				rec->file_flags & FILE_ATTR_DUP_FILE_NAME_INDEX_PRESENT ? L"<DIR>" : L" ", rec->real_sz);
			wprintf(L" %.*ls\n", rec->filename_len, (wchar_t*)(rec->filename));
		}
	}
	DeleteString(str_buf);
	CloseIndexIterator(iter);
}


bool ExtractAttributes(execution_context context, const string file_name, uint64_t mft_ref)
{
	mft_ref &= 0x00000000FFFFFFFF;
	wchar_t* anon_name = L"unknown";
	mft_file file = LoadMFTFile(context, mft_ref);

	if (!file)
		return false;
	
	string local_name = NULL;
	if (!file_name || StringLen(file_name) == 0)
	{
		local_name = PreferedFileName(context, file);
		if (!local_name)
		{
			printf("Warning: no usable filename found for MFT #%lld and no alternative specified in command line. ", mft_ref);
			printf("Using %ls instead.\n", anon_name);
			local_name = StringPrint(NULL, 0, L"%ls", anon_name);
			if (!local_name)
				return CleanUpAndFail(DeleteMFTFile, file, "");
		}
	}
	
	string file_path = context->parameters->tcp_send ? NewString() :
			StringPrint(NULL, 0, L"%ls\\", BaseString(context->parameters->output_folder));
	rsize_t path_offs = StringLen(file_path);
	bool result = true;
	uint32_t last_type = ATTR_ATTRIBUTE_END_MARKER;
	uint32_t type_cntr = 0;
	for (attribute at = FirstAttribute(context, file, 0xFFFF); at; at = NextAttribute(context, file, at, 0xFFFF))
	{
		if (at->type == ATTR_DATA && at->name_len == 0)
			StringPrint(file_path, path_offs, L"%ls",  BaseString(local_name ? local_name : file_name));
		else if (at->type == ATTR_DATA)
			StringPrint(file_path, path_offs, L"%ls[ADS_%.*ls]", BaseString(local_name ? local_name : file_name),
				at->name_len, (wchar_t*)((uint8_t*)at + at->name_offs));
		else if (!context->parameters->all_attribs)
			continue;
		else
		{
			if (at->type != last_type)
			{
				type_cntr = 0;
				if (NextAttribute(context, file, at, AttrTypeFlag(at->type)))
					type_cntr++;
			}
			last_type = at->type;
			StringPrint(file_path, path_offs, L"%ls_%lld_%ls", BaseString(local_name ? local_name : file_name), mft_ref, AttributeTypeName(at->type));
			if (at->name_len > 0)
				StringPrint(file_path, StringLen(file_path), L"_%.*ls", at->name_len, (wchar_t*)((uint8_t*)at + at->name_offs));
			if (type_cntr > 0)
				StringPrint(file_path, StringLen(file_path), L"_%d", type_cntr++);
			StringPrint(file_path, StringLen(file_path), L".bin");
		}

		if (!(result = WriteAttributeContent(context, file, at, file_path)))
			break;
	}
	DeleteString(file_path);
	DeleteMFTFile(file);
	if (local_name)
		DeleteString(local_name);
	return result;
}

bool WriteAttributeContent(execution_context context, mft_file file, attribute at, const string file_name)
{
	const uint64_t read_block_sz = 0x20000;

	//Make sure we have a data writer:
	if (!context->writer)
	{
		context->writer = context->parameters->tcp_send ? TCPWriter(context->parameters->ip_address, context->parameters->tcp_port) : FileWriter(file_name);
		if (!context->writer)
			return false;
	}

	wprintf(context->parameters->tcp_send ? L"Tcpsending: %ls\n" : L"Writing: %ls\n", BaseString(file_name));

	attribute_reader rdr = OpenAttributeReader(context, file, at);
	bytes read_buffer = CreateEmpty();
	uint64_t bytes_read = 0;
	bool result = true;

	for (result = AppendBytesFromAttribRdr(context, rdr, 0, read_block_sz, read_buffer, 0); result && bytes_read < AttributeSize(at);
		result = AppendBytesFromAttribRdr(context, rdr, -1, read_block_sz, read_buffer, 0))
	{
		WriteData(context->writer, read_buffer);
		bytes_read += read_buffer->buffer_len;
	}
	DeleteBytes(read_buffer);

	CloseAttributeReader(rdr);

	if (!context->parameters->tcp_send)
	{
		CloseDataWriter(context->writer);
		context->writer = NULL;
	}

	return result;
}

string PreferedFileName(execution_context context, mft_file file)
{
	attribute best = FirstAttribute(context, file, AttrTypeFlag(ATTR_FILE_NAME));
	for (attribute at = best; at; at = NextAttribute(context, file, at, AttrTypeFlag(ATTR_FILE_NAME)))
	{
		if (((file_name_attribute)((uint8_t*)best + best->value_offs))->name_space != FILE_NAME_DOS)
		{
			best = at;
			break;
		}
	}
	if (!best)
		return NULL;

	return StringPrint(NULL, 0, L"%.*ls", ((file_name_attribute)((uint8_t*)best + best->value_offs))->file_name_length,
		(wchar_t *)((file_name_attribute)((uint8_t*)best + best->value_offs))->file_name);
}

bool PrintNTFSDate(uint64_t date, string str, rsize_t offset)
{
	SYSTEMTIME system_tm;
	if (!FileTimeToSystemTime((FILETIME*)&date, &system_tm))
		return false;

	SYSTEMTIME loc_system_tm;
	if (!SystemTimeToTzSpecificLocalTime(NULL, &system_tm, &loc_system_tm))
		return false;

	Reserve(str, offset + (offset + 21) * sizeof(wchar_t));
	int date_ln = GetDateFormatW(LOCALE_USER_DEFAULT, 0, &loc_system_tm, L"dd'-'MMM'-'yyyy ", (wchar_t *)(str->buffer + offset*sizeof(wchar_t)), 13);

	GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &loc_system_tm, L"HH':'mm':'ss", (wchar_t *)(str->buffer + (offset + date_ln - 1) * sizeof(wchar_t)), 9);

	return true;
}

