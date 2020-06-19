#include "attribs.h"
#include "datarun.h"
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
#pragma pack (push, 1)
struct _attribute_list {
	/*Ofs*/
	/*  0*/	uint32_t type;				// Type of referenced attribute. 
	/*  4*/	uint16_t length;			// Byte size of this entry (8-byte aligned). 
	/*  6*/	uint8_t  name_len;			// Size in Unicode chars of the name of the
										//	attribute or 0 if unnamed.
	/*  7*/	uint8_t name_offs;			// Byte offset to beginning of attribute name 
	/*  8*/	int64_t start_vcn;			// Lowest virtual cluster number of this portion
										// of the attribute value. This is usually 0.
	/* 16*/	uint64_t mft_ref;			// The reference of the mft record holding
										// the ATTR_RECORD for this portion of the
										// attribute value.
	/* 24*/	uint16_t attr_id;		// If lowest_vcn = 0, the instance of the
										// attribute being referenced; otherwise 0.
	/* 26*/	uint8_t name[0];
} ;
#pragma pack(pop)
typedef struct _attribute_list* attribute_list;

#pragma pack (push, 1)
struct _file_name_attribute {
	/*  0*/	uint64_t parent_directory;			/* Directory this filename is referenced from. */
	/*  8*/	uint64_t creation_time;				/* Time file was created. */
	/* 10*/	uint64_t last_data_change_time;		/* Time the data attribute was last modified. */
	/* 18*/	uint64_t last_mft_change_time;		/* Time this mft record was last modified. */
	/* 20*/	uint64_t last_access_time;			/* Time this mft record was last accessed. */
	/* 28*/	uint64_t allocated_size;			/* Byte size of on-disk allocated space for the unnamed data attribute. */
	/* 30*/	uint64_t data_size;					/* Byte size of actual data in unname data attribute.  For a directory or
												   other inode without an unnamed $DATA attribute, this is always 0. */
	/* 38*/	uint32_t file_attributes;			/* Flags describing the file. */
	/* 3c*/	union {
		/* 3c*/	struct {
			/* 3c*/	uint16_t packed_ea_size;		/* Size of the buffer needed to pack the extended attributes
													   (EAs), if such are present.*/
			/* 3e*/	uint16_t reserved;				/* Reserved for alignment. */
		};
		/* 3c*/	struct {
			/* 3c*/	uint32_t reparse_point_tag;		/* Type of reparse point, present only in reparse points and only
													   if there are no EAs. */
		};
	};
	/* 40*/	uint8_t file_name_length;				/* Length of file name in (Unicode) characters. */
	/* 41*/	uint8_t name_space;						/* Namespace of the file name. */
	/* 42*/	uint8_t file_name[0];					/* File name in Unicode. */
};
#pragma pack(pop)

const UT_icd UINT64_icd = { sizeof(uint64_t), NULL, NULL, NULL };

const wchar_t *namespaces[] = { L"POSIX", L"WIN32", L"DOS", L"WIN32 + DOS" };

wchar_t* AttributeTypeName(uint32_t type)
{
	return type_names[AttrTypeToIndex(type)];
}

bytes GetAttributeData(cluster_reader cr, attribute attrib)
{
	bytes result;
	if (!attrib->non_resident)
		result = FromBuffer((uint8_t*)attrib + attrib->value_offs, attrib->value_len);
	else //attribute list is found from data run in $AttrList
	{
		UT_array* rl = EmptyRunList();
		ExtractRunsFromAttribute(rl, attrib);
		result = CreateEmpty();
		bytes clust = CreateEmpty();
		for (run* cur = utarray_front(rl); cur; cur = utarray_next(rl, cur))
		{
			ReadClustersIn(cr, cur->vcn, cur->clusters, clust);
			Append(result, clust, 0, clust->buffer_len);
		}
		utarray_free(rl);
		DeleteBytes(clust);
		RightTrim(result, result->buffer_len - (rsize_t)attrib->real_sz);
	}
	return result;
}

UT_array* GetAttributeListRecords(cluster_reader cr, uint64_t file_ref, const attribute attrib)
{
	bytes at_lst = GetAttributeData(cr, attrib);
	UT_array* result;
	utarray_new(result, &UINT64_icd);
	for (uint64_t at_offset = 0; at_offset < at_lst->buffer_len; )
	{
		attribute_list ent = (attribute_list)(at_lst->buffer + at_offset);
		at_offset += ent->length;
		uint64_t loc_ref = ent->mft_ref & 0x0000FFFFFFFFFFFF;
		if (loc_ref == file_ref)
			continue;

		uint64_t* val;
		for (val = (uint64_t*)utarray_front(result); val; val = (uint64_t*)utarray_next(result, val))
			if (*val == loc_ref)
				break;
		if (!val)
			utarray_push_back(result, &(loc_ref));

	}
	DeleteBytes(at_lst);
	return result;
}

wchar_t* GetAttributeName(attribute attrib)
{
	return NtfsNameExtract((uint8_t *)attrib + attrib->name_offs,  (rsize_t)attrib->name_len);
}

wchar_t* NameFromNameAttr(attribute name_attr)
{
	file_name_attribute fn = (file_name_attribute)((uint8_t*)name_attr + name_attr->value_offs);
	return NtfsNameExtract(fn->file_name, fn->file_name_length);
}

uint8_t NameSpaceFromNameAttr(attribute name_attr)
{
	return ((file_name_attribute)((uint8_t*)name_attr + name_attr->value_offs))->name_space;
}

attribute AttributePtr(bytes buf, size_t offset)
{
	attribute result = (attribute)(((uint8_t *)buf->buffer) + offset);
	if (result->type != ATTR_ATTRIBUTE_END_MARKER && offset + result->length > buf->buffer_len)
	{
		return ErrorCleanUp(NULL, NULL, "Buffer does not contain valid attribute.\n");
	}
	return result;
}

const wchar_t* NameSpaceLabel(uint8_t ns)
{
	return namespaces[ns];
}


