#include "attribs.h"
#include "helpers.h"

wchar_t* type_names[] = {
	L"$STANDARD_INFORMATION",
	L"$ATTRIBUTE_LIST",
	L"$FILE_NAME",
	L"$OBJECT_ID",
	L"$SECURITY_DESCRIPTOR",
	L"$VOLUME_NAME",
	L"$VOLUME_INFORMATION",
	L"$DATA",
	L"$INDEX_ROOT",
	L"$INDEX_ALLOCATION",
	L"$BITMAP",
	L"$REPARSE_POINT",
	L"$EA_INFORMATION",
	L"$EA",
	L"$PROPERTY_SET",
	L"$LOGGED_UTILITY_STREAM"
};

const wchar_t* namespaces[] = { L"POSIX", L"WIN32", L"DOS", L"WIN32 + DOS" };


run_list_iterator StartRunListIterator(const attribute attrib)
{
	SafeCreate(result, run_list_iterator);
	result->end_of_runs = false;
	result->cur_lcn = 0;
	result->prev_lcn = 0;
	result->next_vcn = attrib->start_vcn;
	result->next_index = (uint8_t*)attrib + attrib->run_list_offs;
	NextRun(result);
	return result;
}

bool NextRun(run_list_iterator iter)
{
	if (iter->end_of_runs || !*iter->next_index)
	{
		iter->end_of_runs = true;
		return false;
	}
	iter->cur_vcn = iter->next_vcn;
	uint8_t v = *(iter->next_index) & 0x0f;
	uint8_t l = (*(iter->next_index)++ & 0xf0) >> 4;
	iter->next_vcn += ParseSigned(iter->next_index, v);
	if (iter->cur_lcn)
		iter->prev_lcn = iter->cur_lcn;
	if (l == 0)
		iter->cur_lcn = 0;
	else
		iter->cur_lcn = (int64_t)iter->prev_lcn + ParseSigned(iter->next_index + v, l);

	iter->next_index += v + l;
	return true;
}

void CloseRunListIterator(run_list_iterator iter) { free(iter); }
