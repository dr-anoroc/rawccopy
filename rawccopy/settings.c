#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>


#include "Shlwapi.h"
#include "ut-wrapper.h"
#include "settings.h"
#include "fileio.h"
#include "helpers.h"
#include "network.h"

typedef struct _volume
{
	wchar_t *type;
	uint64_t offset;
	uint64_t size;
} volume;


void PrintHelp();
bool SetUpImageFile(const uint64_t vol_index, settings set);
UT_array *CheckMBR(const wchar_t* image_file);
void CheckGPT(const file_reader fr, const wchar_t* image_file, UT_array* list);
void CheckExtendedPartition(const file_reader fr, uint64_t offset, const wchar_t* image_file, UT_array* list);
bool TestNTFS(const file_reader fr, uint64_t start_sector, UT_array* list, const wchar_t* image_file);
bool HasNTFS(const UT_array* list);

void AddVolume(UT_array* list, const wchar_t* type, uint64_t offset, uint64_t size);

wchar_t* EnsurePrefix(wchar_t* path);


void DeleteVol(volume* vol);

static const UT_icd volume_icd = { sizeof(volume), NULL, NULL, DeleteVol };

settings Parse(int argc, char* argv[])
{

	if (argc == 1)
	{
		PrintHelp();
		return NULL;
	}
	   
	char* file_name_path, *out_path, *all_attr, *image_file, *image_volume, *raw_dir_mode, *write_fs_info, *out_name, *tcp_send;
	file_name_path = out_path = all_attr = image_file = image_volume = raw_dir_mode = write_fs_info = out_name = tcp_send = NULL;

	for (int i = 1; i < argc; ++i)
	{
		if (!strncmp_nocase(argv[i], "/FileNamePath:", 14))
			file_name_path = argv[i] + 14;
		else if (!strncmp_nocase(argv[i], "/OutputPath:",12))
			out_path = argv[i] + 12;
		else if (!strncmp_nocase(argv[i], "/AllAttr:",9))
			all_attr = argv[i] + 9;
		else if (!strncmp_nocase(argv[i], "/ImageFile:", 11))
			image_file = argv[i] + 11;
		else if (!strncmp_nocase(argv[i], "/ImageVolume:", 13))
			image_volume = argv[i] + 13;
		else if (!strncmp_nocase(argv[i], "/RawDirMode:", 12))
			raw_dir_mode = argv[i] + 12;
		else if (!strncmp_nocase(argv[i], "/WriteFSInfo:", 13))
			write_fs_info = argv[i] + 13;
		else if (!strncmp_nocase(argv[i], "/OutputName:", 12))
			out_name = argv[i] + 12;
		else if (!strncmp_nocase(argv[i], "/TcpSend:", 9))
			tcp_send = argv[i] + 9;
	}

	SafeCreate(result, settings);
	memset(result, 0, sizeof(*result));

	result->tcp_send = (tcp_send && *tcp_send);

	if (out_path && *out_path)
	{
		if (result->tcp_send)
		{
			if (!ParseIPDestination(out_path, result->ip_addr, result->tcp_port))
				ErrorExit("Error: Configuration of TcpSend failed.\n", -1);
		}
		else if (PathFileExistsA(out_path))
			result->output_folder = ToWideString(out_path, 0);
	}
	if (!result->output_folder || !*result->output_folder)
	{
		SafePtrBlock(buffer, wchar_t*, MAX_NTFS_PATH);
			
		DWORD cnt = GetModuleFileNameW(NULL, buffer, (DWORD)MAX_NTFS_PATH - 1);

		wchar_t* file_pt = wcsrchr(buffer, L'\\');
		if (!file_pt)
			ErrorExit("Invalid output path.\n", -1);
		else
		{
			*file_pt = 0;
			result->output_folder = buffer;
		}
	}

	if (out_name && *out_name)
	{
		result->output_file = ToWideString(out_name, 0);
		wchar_t* left_pt = wcsrchr(result->output_file, L'\\');
		wchar_t* write = result->output_file;
		wchar_t* scan = left_pt ? left_pt + 1 : write;
		for (; ; ++scan)
		{
			wchar_t* next = wcspbrk(scan, L"\\:*?\"\"<>");
			if (next == NULL)
				break;
			for (;scan < next; ++scan)
				*write++ = *scan;
		}
		do 	{ *write++ = *scan;	} while (*scan++);
	}

	if (all_attr && *all_attr)
		result->all_attribs = (*all_attr == '1');

	result->detail_mode = 0;
	if (raw_dir_mode && *raw_dir_mode)
	{
		if (*raw_dir_mode < '0' || *raw_dir_mode > '2')
		{
			ErrorCleanUp(DeleteSettings, result, "Error: RawDirMode must be an integer from 0 - 2.\n");
			PrintHelp();
			return NULL;
		}
		result->detail_mode = *raw_dir_mode - '0';
	}

	uint64_t image_vol = 0;
	if (image_volume && *image_volume)
	{
		image_vol = strtoul(image_volume, NULL, 10);
		if (!image_vol)
		{
			ErrorCleanUp(DeleteSettings, result,"Error: ImageNtfsVolume must be a digit starting from 1.\n");
			PrintHelp();
			return NULL;
		}

	}

	result->write_boot_info = false;
	if (write_fs_info && *write_fs_info)
	{
		if (*write_fs_info != '0' && *write_fs_info != '1')
			printf("Error: WriteFSInfo had unexpected value. Skipping it.\n");
		else
			result->write_boot_info = *write_fs_info == '1';
	}

	if (image_file && *image_file)
	{
		if (!PathFileExistsA(image_file))
			return ErrorCleanUp(DeleteSettings, result, "Error: Image file not found: %s\n", image_file);
		
		StringAssign(result->target_drive, ToWideString(image_file, 0));
		if (!SetUpImageFile(image_vol, result))
			return ErrorCleanUp(DeleteSettings, result, "");
	}

	if (!file_name_path || !*file_name_path)
	{
		return ErrorCleanUp(DeleteSettings, result, "Error: FileNamePath param not specified.\n");
	}

	char* pt1;
	char* pt2;
	char* pt3;

	if (LazyParse(file_name_path, "Harddisk", &pt1, "Partition", &pt2, ":", &pt3, NULL))
	{
		result->is_phys_drv = (pt2 - pt1 - 8 == 1 && isdigit(*(pt2 - 1)) && pt3 - pt2 - 9 == 1 && isdigit(*(pt3 - 1)));
	}

	if (LazyParse(file_name_path, "PhysicalDrive", &pt1, ":", &pt2, NULL))
	{
		result->is_phys_drv = (pt2 - pt1 - 13 == 1 && isdigit(*(pt2 - 1)));

		result->target_drive = ToWideString(file_name_path, (size_t)(pt2 - file_name_path));

		//if it's not physical it must be a file, so verify that it exists and handle it
		//as an image, sort of...
		if (!result->is_phys_drv && !PathFileExistsW(result->target_drive))
		{
			return ErrorCleanUp(DeleteSettings, result, "Error: Image file not found: %s\n", result->target_drive);
		}
		else if (!SetUpImageFile(image_vol, result))
		{
			return ErrorCleanUp(DeleteSettings, result, "");
		}
		//Very strange, but it's like that in the original code:
		result->is_image = false;
		StringAssign(result->target_drive, ToWideString(file_name_path, 0));
	}

	if (LazyParse(file_name_path, "HarddiskVolume", &pt1, ":", &pt2, NULL))
	{
		result->is_phys_drv = (pt2 - pt1 - 14 == 1 && isdigit(*(pt2 - 1)));
	}

	if (LazyParse(file_name_path, "HarddiskVolumeShadowCopy", &pt1, ":", &pt2, NULL))
	{
		result->is_phys_drv = (pt2 - pt1 - 24 == 1 && isdigit(*(pt2 - 1)));
	}

	if (result->is_phys_drv)
	{
		LazyParse(file_name_path, ":", &pt1, NULL);
		StringAssign(result->target_drive, ToWideString(file_name_path, (size_t)(pt1 - file_name_path)));

		if (IsDigits(++pt1))
		{
			SafeAlloc(result->mft_ref,1);
			*(result->mft_ref) = strtoul(pt1, NULL, 10);
		}
		else
			result->target_path = ToWideString(pt1, 0);
	}
	else
	{
		if (!result->is_image)
		{
			if (!PathFileExistsA(file_name_path))
			{
				if (file_name_path[1] == ':')
				{
					StringAssign(result->target_drive, ToWideString(file_name_path, 2));
					if (!IsDigits(file_name_path + 2))
					{
						printf("Warning: File not found with regular file search: %s\n", file_name_path);
						StringAssign(result->target_path, ToWideString(file_name_path, 0));
					}
					else
					{
						SafeAlloc(result->mft_ref,1);
						*(result->mft_ref) = strtoul(file_name_path + 2, NULL, 10);
					}
				}
				else
				{
					return ErrorCleanUp(DeleteSettings, result, "Error: File probably locked.\n");
				}
			}
			else
			{
				StringAssign(result->target_path, ToWideString(file_name_path, 0));
				DWORD at = GetFileAttributesW(result->target_path);
				if (at == INVALID_FILE_ATTRIBUTES)
					return ErrorCleanUp(DeleteSettings, result, "Error: Could not retrieve file attributes.\n");

				result->is_folder = at & FILE_ATTRIBUTE_DIRECTORY;
				StringAssign(result->target_drive, ToWideString(file_name_path, 2));
			}
		}
		else
		{
			if (result->target_path[0] == L'\\')
				StringAssign(result->target_path, JoinStrings(L"", L"x:", result->target_path, NULL));
			if (file_name_path[1] == ':')
			{
				result->target_path = ToWideString(file_name_path, 0);


				if (IsDigitsW(result->target_path + 2))
				{
					SafeAlloc(result->mft_ref,1);
					*(result->mft_ref) = wcstoul(result->target_path + 2, NULL, 10);
				}
			}
			else
			{
				//very weird, probalby wrong error message:
				return ErrorCleanUp(DeleteSettings, result, "Error: File probably locked.\n");
			}

		}
		StringAssign(result->target_drive, EnsurePrefix(result->target_drive));
	}
	return result;
}

wchar_t *EnsurePrefix(wchar_t* path)
{
	wchar_t* prefix = L"\\\\.\\";
	if (wcsncmp(path, prefix, 4))
	{
		return  JoinStrings(L"", prefix, path, NULL);
	}
	return path;
}

bool SetUpImageFile(const uint64_t vol_index, settings set)
{
	StringAssign(set->target_drive, EnsurePrefix(set->target_drive));
	bool result = true;

	UT_array* vols = CheckMBR(set->target_drive);
	if (!vols)
		return false;
	if (!HasNTFS(vols))
	{
		printf("Sorry, no NTFS volume found in that file.\n");
		set->is_image = false;
	}
	else if (vol_index > 0)
	{
		set->is_image = true;
		volume* vol = ((volume*)utarray_eltptr(vols, vol_index - 1));
		if (!vol)
		{
			printf("Error: Volume %lld does not exist in image.\n", vol_index);
			printf("Found volumes are:\n");
			int i = 1;
			for (volume* v = utarray_front(vols); v; v = utarray_next(vols, v))
				wprintf(L"Volume %d, StartOffset %lld, Size %.2lf GB\n", i++, v->offset,
					(double)((double)(v->size) / 2.0 / 1024.0 / 1024.0));
			PrintHelp();
			result = false;
		}
		else
			set->image_offs = vol->offset;
	}
	utarray_free(vols);
	return result;
}


UT_array* CheckMBR(const wchar_t* image_file)
{
	file_reader fr = OpenFileReader(image_file);
	if (!fr)
		return NULL;

	bytes sector = ReadNextBytes(fr, 512);
	if (!sector)
		return NULL;
	
	UT_array* result;
	utarray_new(result, &volume_icd);

	for	(rsize_t part_ind = 446; part_ind < 512; part_ind += 16)
	{
		if (Same(sector, part_ind, 0, 16))
			break;
		unsigned char file_system_desc = Byte(sector,part_ind + 4);
		uint64_t start_sector = ReadNumber(sector,part_ind + 8, 4);
		uint64_t num_sectors = ReadNumber(sector, part_ind + 12, 4);

		if (file_system_desc == 0xEE && start_sector == 1 && num_sectors == 4294967295)
			//A typical dummy partition to prevent overwriting of GPT data, also known as "protective MBR"
			CheckGPT(fr, image_file, result);
		else if (file_system_desc == 0x05 || file_system_desc == 0x0F)
			//Extended partition
			CheckExtendedPartition(fr, start_sector, image_file, result);
		else if (!TestNTFS(fr, start_sector, result, image_file))
		{
			AddVolume(result, L"Non-NTFS", start_sector, num_sectors);
		}
	}
	if (!HasNTFS(result)) //Also check if pure partition image (without mbr)
	{
		TestNTFS(fr, 0, result, image_file);
	}
	CloseFileReader(fr);
	DeleteBytes(sector);
	return result;
}

void CheckExtendedPartition(const file_reader fr, uint64_t offset, const wchar_t* image_file, UT_array* list)
{
	bytes buffer = CreateBytes(512);

	for (uint64_t next_entry = 0; ;)
	{
		if(!ReadBytesIn(buffer, fr, (offset + next_entry) * 512, 512))
			break;

		uint32_t part_table_offs = 0x1BE;

		unsigned char file_syst_desc = Byte(buffer, (rsize_t)part_table_offs + 4);

		uint64_t starting_sector = offset + next_entry + ReadNumber(buffer, (rsize_t)part_table_offs + 8, 4);

		uint64_t num_sectors = ReadNumber(buffer, (rsize_t)part_table_offs + 12, 4);

		if (file_syst_desc == 0x06 || file_syst_desc == 0x07)
		{
			if (!TestNTFS(fr, starting_sector, list, image_file))
				AddVolume(list, L"Non-NTFS", starting_sector, num_sectors);
		}
		else if (file_syst_desc != 0x05 && file_syst_desc != 0x0F)
		{
			AddVolume(list, L"Non-NTFS", starting_sector, num_sectors);
		}
		if (Same(buffer, (rsize_t)part_table_offs + 16, 0, 16))
			break; //No more entries

		next_entry = ReadNumber(buffer, (rsize_t)part_table_offs + 24, 4);

	}
	DeleteBytes(buffer);

}

void CheckGPT(const file_reader fr, const wchar_t* image_file, UT_array* list)
{
	bytes buffer = ReadBytes(fr, 512, 512);
	if (!buffer)
		return;

	if (!EqualsBuffer(buffer, 0, "\x45\x46\x49\x20\x50\x41\x52\x54", 8))
	{
		wchar_t* rec_dump = ToString(buffer);
		wprintf(L"Error: Could not find GPT signature: %s\n", rec_dump);
		free(rec_dump);
		DeleteBytes(buffer);
		return;
	}
	uint64_t StartLBA = ReadNumber(buffer, 72,8);
	uint32_t num_partitions = (uint32_t)ReadNumber(buffer, 80, 4);
	uint32_t partition_entry_sz = (uint32_t)ReadNumber(buffer, 84, 4);

	DeleteBytes(buffer);
	buffer = ReadBytes(fr, StartLBA * 512, (uint64_t)num_partitions * partition_entry_sz);
	if (buffer == 0)
		return;

	for (uint32_t i = 0; i < partition_entry_sz; ++i)
	{
		uint64_t first_lba = ReadNumber(buffer, 32 + i * (rsize_t)partition_entry_sz, 8); 
		uint64_t last_lba = ReadNumber(buffer, 40 + i * (rsize_t)partition_entry_sz, 8);
		if (first_lba == 0 && last_lba == 0)
			break; // No more entries
		if (!TestNTFS(fr, first_lba, list, image_file))
			AddVolume(list, L"Non-NTFS", first_lba, last_lba - first_lba);
	}
	DeleteBytes(buffer);
}

bool TestNTFS(const file_reader fr, uint64_t start_sector, UT_array* list, const wchar_t* image_file)
{
	bytes buffer = ReadBytes(fr, start_sector * 512, 512);
	if (!buffer)
		return false;

	uint64_t total_sectors = ReadNumber(buffer, 40, 8);
	if (EqualsBuffer(buffer, 3, "\x4E\x54\x46\x53", 4))
	{
		AddVolume(list, L"NTFS", start_sector * 512, total_sectors);
		DeleteBytes(buffer);
		return true; // Volume is NTFS
	}

	DeleteBytes(buffer);
	wprintf(L"Error: Could not find NTFS on %ls at offset %lld\n", image_file, start_sector * 512);
	return false;
}

bool HasNTFS(const UT_array* list)
{
	for (volume *v = utarray_front(list); v; v = utarray_next(list, v))
		if (!wcscmp(v->type, L"NTFS"))
			return true;
	return false;
}


void PrintHelp()
{
	printf("Syntax:\n");
	printf("RawCCopy /ImageFile:FullPath\\ImageFilename /ImageVolume:[1,2...n] /FileNamePath:FullPath\\Filename /OutputPath:FullPath /OutputName:FileName /AllAttr:[0|1] /RawDirMode:[0|1|2] /WriteFSInfo:[0|1]\n");
	printf("Examples:\n");
	printf("RawCCopy /FileNamePath:c:\\hiberfil.sys /OutputPath:e:\\temp /OutputName:hiberfil_c.sys\n");
	printf("RawCCopy /FileNamePath:c:\\pagefile.sys /OutputPath:e:\\temp /AllAttr:1\n");
	printf("RawCCopy /FileNamePath:c:0 /OutputPath:e:\\temp /OutputName:MFT_C\n");
	printf("RawCCopy /ImageFile:e:\\temp\\diskimage.dd /ImageVolume:2 /FileNamePath:c:2 /OutputPath:e:\\out\n");
	printf("RawCCopy /ImageFile:e:\\temp\\partimage.dd /ImageVolume:1 /FileNamePath:c:\\file.ext /OutputPath:e:\\out\n");
	printf("RawCCopy /FileNamePath:c:\\$Extend /RawDirMode:1\n");
	printf("RawCCopy /ImageFile:e:\\temp\\diskimage.dd /ImageVolume:2 /FileNamePath:""c:\\system volume information"" /RawDirMode:2 /WriteFSInfo:1\n");
	printf("RawCCopy /FileNamePath:\\\\.\\HarddiskVolumeShadowCopy1:x:\\ /RawDirMode:1\n");
	printf("RawCCopy /FileNamePath:\\\\.\\Harddisk0Partition2:0 /OutputPath:e:\\out /OutputName:MFT_Hd0Part2\n");
	printf("RawCCopy /FileNamePath:\\\\.\\PhysicalDrive0:0 /ImageVolume:2 /OutputPath:e:\\out\n");
	printf("RawCCopy /FileNamePath:c:\\$LogFile /TcpSend:1 /OutputPath:10.10.10.10:6666\n");
}


void DeleteSettings(settings set)
{
	if (set->mft_ref)
		free(set->mft_ref);

	if (set->output_folder)
		free(set->output_folder);

	if (set->output_file)
		free(set->output_file);

	if (set->target_path)
		free(set->target_path);

	if (set->target_drive)
		free(set->target_drive);

	free(set);
}

void DeleteVol(volume* vol)
{
	if (vol->type)
		free(vol->type);
}

void AddVolume(UT_array* list, const wchar_t* type, uint64_t offset, uint64_t size)
{
	volume newV = { DuplicateString(type), offset, size };
	utarray_push_back(list, &newV);
}

