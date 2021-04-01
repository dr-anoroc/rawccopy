#include "path.h"
#include "mft.h"
#include "index.h"
#include "attribs.h"


const uint32_t SYMLINK_FLAG_RELATIVE = 0x00000001;  /* The substitute name is a path name relative to the
													   directory containing the symbolic link. */

#pragma pack (push, 1)
struct _reparse_point {
	/*0x00*/ uint32_t reparse_tag;				/* Reparse tag, must be IO_REPARSE_TAG_SYMLINK or IO_REPARSE_TAG_MOUNT_POINT*/
	/*0x04*/ uint16_t data_size;				/* Size, in bytes, of the reparse data that follows
												   the common portion of the path_buffer element.
												   This value is the length of the data starting at the
												   subst_name_offs field (or the size of the path_buffer field, plus 12) */
	/*0x06*/ uint16_t reserved;
	/*0x08*/ uint16_t subst_name_offs;			/* Offset, in bytes, of the substitute name string in the PathBuffer array */
	/*0x0a*/ uint16_t subst_name_len;			/* Length, in bytes, of the substitute name string in the PathBuffer array */
	/*0x0c*/ uint16_t print_name_offs;			/* Offset, in bytes, of the print name string in the PathBuffer array */
	/*0x0e*/ uint16_t print_name_len;			/* Length, in bytes, of the print name string in the PathBuffer array */
	union {
		struct {
			/*0x10*/ uint32_t flags;			/* SYMLINK_FLAG_RELATIVE or 0 */
			/*0x14*/ uint8_t link_buffer[1];	/* Buffer containing names */
		};
		/*0x10*/ uint8_t mount_buffer[1];		/* Buffer containing names */
	};
};
#pragma pack(pop)
typedef struct _reparse_point* reparse_point;

bytes GetLinkedEntry(const execution_context context, const resolved_path pt, const index_entry link);

resolved_path CopyPath(const resolved_path pt);

bool ExtendPath(const execution_context context, const wchar_t* path, resolved_path* result);

resolved_path FollowLink(const execution_context context, const reparse_point link, const resolved_path pt);

string GetSubstitutionPath(const reparse_point link);

bool IsSymlinkCompatible(const execution_context context, const string target);

path_step CreateStep(const bytes original, const bytes deref);

void DeleteStep(path_step step)
{
	if (step->dereferenced)
		DeleteBytes(step->dereferenced);
	if (step->original)
		DeleteBytes(step->original);
}

static const path_step dummy;
static const UT_icd ut_step_icd = { sizeof(*dummy) , NULL, NULL, DeleteStep };

resolved_path CopyPath(const resolved_path pt)
{
	resolved_path result;
	utarray_new(result, &ut_step_icd);
	SafeCreate(new_step, path_step);
	for (path_step st = utarray_front(pt); st; st = utarray_next(pt, st))
	{
		new_step->dereferenced = st->dereferenced ? CopyBuffer(st->dereferenced) : NULL;
		new_step->original = st->original ? CopyBuffer(st->original) : NULL;
		utarray_push_back(result, new_step);
	}
	free(new_step);
	return result;
}

resolved_path EmptyPath(const execution_context context)
{
	resolved_path result;
	utarray_new(result, &ut_step_icd);
	SafeCreate(step, path_step);
	step->dereferenced = NULL;
	step->original = FindIndexEntry(context, ROOT_FILE_NAME_INDEX_NUMBER, L".");
	utarray_push_back(result, step);
	free(step);
	return result;
}

bool GoDown(execution_context context, resolved_path pt, const wchar_t* item)
{
	path_step last = utarray_back(pt);
	if (!last)
		return CleanUpAndFail(NULL, NULL, "Found path without root element.\n");

	bytes next_orig = FindIndexEntry(context, IndexEntryPtr(DerefStep(last))->mft_reference, item);
	if (!next_orig)
		return false;

	bytes next_deref = NULL;
	if (TYPE_CAST(next_orig, index_entry)->file_flags & FILE_ATTRIBUTE_REPARSE_POINT)
	{
		next_deref = GetLinkedEntry(context, pt, IndexEntryPtr(next_orig));
		if (!next_deref)
			return CleanUpAndFail(DeleteBytes, next_orig, "");
	}

	path_step next = CreateStep(next_orig, next_deref);
	if (next)
	{
		utarray_push_back(pt, next);
		free(next);
		return true;
	}
	else
		return false;
}

bool GoUp(resolved_path pt)
{
	if (utarray_len(pt) <= 1)
		return CleanUpAndFail(NULL, NULL, "Error: following '..' not possible above root\n");

	utarray_pop_back(pt);
	return true;
}

bool TryParsePath(execution_context context, const wchar_t* path, resolved_path* result)
{
	*result = EmptyPath(context);
	
	wchar_t* rel_path = wcschr(path, L'\\');
	if (rel_path)
		return ExtendPath(context, rel_path + 1, result);
	else
		return true;
}

bool ExtendPath(const execution_context context, const wchar_t* path, resolved_path* result)
{
	string path_cp = StringPrint(NULL, 0, L"%ls", path);
	//'path_cp' is null terminated, or we wouldn't be here,
	//so wcstok is safe
	wchar_t* parser_state;
	bool parse_result = true;
	for (wchar_t* tok = wcstok(BaseString(path_cp), L"\\", &parser_state); tok && parse_result; tok = wcstok(NULL, L"\\", &parser_state))
	{
		if (!wcscmp(L".", tok))
			continue;
		else if (!wcscmp(L"..", tok))
			parse_result = GoUp(*result);
		else
			parse_result = GoDown(context, *result, tok);
	}
	DeleteString(path_cp);

	return parse_result;
}


bytes GetLinkedEntry(const execution_context context, const resolved_path pt, const index_entry link)
{
	mft_file rec = LoadMFTFile(context, link->mft_reference);

	if (!rec)
		return ErrorCleanUp(NULL, NULL, "Record is not a valid link: %lld\n", link->mft_reference);

	attribute at = FirstAttribute(context, rec, AttrTypeFlag(ATTR_REPARSE_POINT));
	if (!at)
		return ErrorCleanUp(DeleteMFTFile, rec, "Record is not a valid link: %lld\n", link->mft_reference);

	bytes raw_link = GetBytesFromAttrib(context, rec, at, 0, AttributeSize(at));

	resolved_path target = FollowLink(context, (reparse_point)(raw_link->buffer), pt);

	bytes result = NULL;
	path_step final;
	if (target && (final = utarray_back(target)))
	{
		result = CopyBuffer(DerefStep(final));
		DeletePath(target);
	}

	DeleteBytes(raw_link);
	DeleteMFTFile(rec);
	return result;
}

resolved_path FollowLink(const execution_context context, const reparse_point link, const resolved_path pt)
{
	string link_path = GetSubstitutionPath(link);
	resolved_path result = NULL;
	if (link->reparse_tag == IO_REPARSE_TAG_MOUNT_POINT)
	{
		//Check for the "\??\" prefix:
		if (wcsncmp(L"\\??\\", BaseString(link_path), 4))
			printf("Unresolvable link path: %ls\n", BaseString(link_path));
		TryParsePath(context, BaseString(link_path) + 4, &result);
	}
	else if (link->reparse_tag == IO_REPARSE_TAG_SYMLINK && (link->flags & SYMLINK_FLAG_RELATIVE))
	{
		result = CopyPath(pt);
		if (!ExtendPath(context, BaseString(link_path), &result))
		{
			DeletePath(result);
			result = NULL;
		}
	}
	else if (link->reparse_tag == IO_REPARSE_TAG_SYMLINK)
	{
		if (IsSymlinkCompatible(context, link_path))
			TryParsePath(context, BaseString(link_path) + 4, &result);
	}
	else
	{
		printf("Invalid link tag: %lx\n", link->reparse_tag);
	}

	DeleteString(link_path);
	return result;
}

string GetSubstitutionPath(const reparse_point link)
{
	return StringPrint(NULL, 0, L"%.*ls", link->subst_name_len / 2,
		(wchar_t*)((link->reparse_tag == IO_REPARSE_TAG_SYMLINK ? link->link_buffer : link->mount_buffer) + link->subst_name_offs));
}

bool IsSymlinkCompatible(const execution_context context, const string target)
{
	//First the limitations on our side:
	if (context->parameters->is_image)
		return CleanUpAndFail(NULL, NULL, "Link resolution failed for [%ls]: Can only resolve links on mounted volumes.\n", BaseString(target));

	//Check for the "\??\" prefix:
	if (wcsncmp(L"\\??\\", BaseString(target), 4))
		return CleanUpAndFail(NULL, NULL, "Unresolvable link path: %ls\n", BaseString(target));


	if (wstrncmp_nocase(BaseString(context->parameters->source_drive) + 4, BaseString(target) + 4, StringLen(context->parameters->source_drive) - 4))
		return CleanUpAndFail(NULL, NULL, "Link resolution failed for [%ls]: links to other volumes than current drive are not supported\n", BaseString(target));


	return true;
}

path_step CreateStep(const bytes original, const bytes deref)
{
	SafeCreate(result, path_step);
	result->dereferenced = deref;
	result->original = original;
	return result;
}