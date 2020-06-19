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
wchar_t* GetSubstitutionPath(const reparse_point link);
bool IsSymlinkCompatible(const execution_context context, const wchar_t* target);

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

bool GoDown(const execution_context context, resolved_path pt, const wchar_t* item)
{
	path_step last = utarray_back(pt);
	if (!last)
	{
		wprintf(L"Found path without root element.\n");
		return false;
	}

	//bytes jump_point = last->dereferenced ? last->dereferenced : last->original;
	bytes next_orig = FindIndexEntry(context, IndexEntryPtr(DerefStep(last))->mft_reference, item);
	if (!next_orig)
		return false;
	//Comments:
	// - If this call fails for some reason, it will return NULL, which, in our logic,
	//	 boils down to treating this as a non-link. Seems a fair trade-off between
	//	 real errors (like not finding the mft record) and non-supported cases (like
	//	 the link pointing to a location outside the volume)
	// - This call will recursively call this function, which handles the -exotic-
	//   link to link scenario. This is, in other words, a fully dereferenced entry.
	bytes next_deref = GetLinkedEntry(context, pt, IndexEntryPtr(next_orig));
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
	{
		//No more entries queued, this means we cannot go higher, '..' is not possible
		wprintf(L"Error: following '..' not possible above root\n");
		return false;
	}
	utarray_pop_back(pt);
	return true;
}

bool TryParsePath(const execution_context context, const wchar_t* path, resolved_path* result)
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
	wchar_t* full_path = DuplicateString(path);
	//'full_path' is null terminated, or we wouldn't be here,
	//so wcstok is safe
	wchar_t* parser_state;
	bool parse_result = true;
	for (wchar_t* tok = wcstok(full_path, L"\\", &parser_state); tok && parse_result; tok = wcstok(NULL, L"\\", &parser_state))
	{
		if (!wcscmp(L".", tok))
			continue;
		else if (!wcscmp(L"..", tok))
			parse_result = GoUp(*result);
		else
			parse_result = GoDown(context, *result, tok);
	}
	free(full_path);

	return parse_result;
}


bytes GetLinkedEntry(const execution_context context, const resolved_path pt, const index_entry link)
{
	if (!(link->file_flags & FILE_ATTRIBUTE_REPARSE_POINT))
		return NULL;

	mft_record rec = AttributesForFile(context, link->mft_reference, AttrTypeFlag(ATTR_REPARSE_POINT));
	if (!rec)
		return ErrorCleanUp(NULL, NULL, "Record is not a valid link: %lld\n", link->mft_reference);

	UT_array* attr = GetAttributes(ATTR_REPARSE_POINT, rec);
	if (!attr)
		return ErrorCleanUp(DeleteMFTRecord, rec, "Record is not a valid link: %lld\n", link->mft_reference);

	bytes raw_at = utarray_front(attr);
	if (!raw_at)
		return ErrorCleanUp(DeleteMFTRecord, rec, "Record is not a valid link: %lld\n", link->mft_reference);

	bytes raw_link = GetAttributeData(context->cr, AttributePtr(raw_at, 0));

	resolved_path target = FollowLink(context, (reparse_point)(raw_link->buffer), pt);

	bytes result = NULL;
	path_step final;
	if (target && (final = utarray_back(target)))
	{
		//bytes resolv = final->dereferenced ? final->dereferenced : final->original;
		result = CopyBuffer(DerefStep(final));
		DeletePath(target);
	}

	DeleteBytes(raw_link);
	DeleteMFTRecord(rec);
	return result;
}

resolved_path FollowLink(const execution_context context, const reparse_point link, const resolved_path pt)
{
	wchar_t* link_path = GetSubstitutionPath(link);
	resolved_path result = NULL;
	if (link->reparse_tag == IO_REPARSE_TAG_MOUNT_POINT)
	{
		//Check for the "\??\" prefix:
		if (wcsncmp(L"\\??\\", link_path, 4))
			printf("Unresolvable link path: %ls\n", link_path);
		TryParsePath(context, link_path + 4, &result);
	}
	else if (link->reparse_tag == IO_REPARSE_TAG_SYMLINK && (link->flags & SYMLINK_FLAG_RELATIVE))
	{
		result = CopyPath(pt);
		if (!ExtendPath(context, link_path, &result))
		{
			DeletePath(result);
			result = NULL;
		}
	}
	else if (link->reparse_tag == IO_REPARSE_TAG_SYMLINK)
	{
		if (IsSymlinkCompatible(context, link_path))
			TryParsePath(context, link_path + 4, &result);
	}
	else
	{
		printf("Invalid link tag: %lx\n", link->reparse_tag);
	}

	free(link_path);
	return result;
}


wchar_t* GetSubstitutionPath(const reparse_point link)
{
	rsize_t buf_len = (rsize_t)link->subst_name_len / 2 + 1;
	SafePtrBlock(path, wchar_t*, buf_len);
	wcsncpy_s(path, buf_len,
		(wchar_t*)((link->reparse_tag == IO_REPARSE_TAG_SYMLINK ? link->link_buffer : link->mount_buffer) + link->subst_name_offs),
		buf_len - 1);
	return path;
}

bool IsSymlinkCompatible(const execution_context context, const wchar_t* target)
{
	//First the limitations on our side:
	if (context->parameters->is_phys_drv || context->parameters->is_image)
	{
		wprintf(L"Link resoltion failed for [%ls]: Can only resolve links on mounted volumes.\n", target);
		return false;
	}
	//Check for the "\??\" prefix:
	if (wcsncmp(L"\\??\\", target, 4))
	{
		printf("Unresolvable link path: %ls\n", target);
		return false;
	}
	wchar_t* drive_end = wcsrchr(context->parameters->target_drive, L':');
	if (!drive_end++)
		return false;

	/*TargetDrive looks like \\.\c: and target looks like \??\c:, so both have a prefix of 4 */
	if (wstrncmp_nocase(context->parameters->target_drive + 4, target + 4, drive_end - context->parameters->target_drive - 4))
	{
		wprintf(L"Link resoltion failed for [%ls]: links to other volumes than current drive are not supported\n", target);
		return false;
	}

	return true;
}

path_step CreateStep(const bytes original, const bytes deref)
{
	SafeCreate(result, path_step);
	result->dereferenced = deref;
	result->original = original;
	return result;
}