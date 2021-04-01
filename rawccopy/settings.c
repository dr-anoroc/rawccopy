#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "safe-string.h"
#include "disk-info.h"
#include "Shlwapi.h"
#include "ut-wrapper.h"
#include "settings.h"
#include "fileio.h"
#include "helpers.h"
#include "network.h"
#include "regex.h"


void PrintHelp();

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
		if (!file_name_path && match("/FileNamePath:", argv[i], &file_name_path))
			continue;
		if (!out_path && match("/OutputPath:", argv[i], &out_path))
			continue;
		if (!all_attr && match("/AllAttr:", argv[i], &all_attr))
			continue;
		if (!image_file && match("/ImageFile:", argv[i], &image_file))
			continue;
		if (!image_volume && match("/ImageVolume:", argv[i], &image_volume))
			continue;
		if (!raw_dir_mode && match("/RawDirMode:", argv[i], &raw_dir_mode))
			continue;
		if (!write_fs_info && match("/WriteFSInfo:", argv[i], &write_fs_info))
			continue;
		if (!out_name && match("/OutputName:", argv[i], &out_name))
			continue;
		if (!tcp_send && match("/TcpSend:", argv[i], &tcp_send))
			continue;
	}

	SafeCreate(result, settings);
	memset(result, 0, sizeof(*result));

	result->tcp_send = (tcp_send && *tcp_send);

	if (out_path && *out_path)
	{
		if (result->tcp_send)
		{
			if (!ParseIPDestination(out_path, &result->ip_address, &result->tcp_port))
				return ErrorCleanUp(DeleteSettings, result, "Error: Configuration of TcpSend failed.\n");
		}
		else if (PathFileExistsA(out_path))
			result->output_folder = StringPrint(NULL, 0, L"%hs", out_path);
	}
	if (!result->output_folder && !(result->output_folder = ExecutablePath()))
		return ErrorCleanUp(DeleteSettings, result, "Invalid output path.\n");

	if (out_name && *out_name)
	{
		unsigned char* fn = strrchr(out_name, '\\');
		if (!fn)
			fn = out_name;
		
		result->output_file = NewString();
		const char* sep = "\\:*?\"\"<>";

		char* pt = out_name;
		for (char *next = strpbrk(pt, sep); next; next = strpbrk(pt, sep))
		{
			StringPrint(result->output_file, StringLen(result->output_file), L"%.*hs", next - pt, pt);
			pt = next + strspn(next, sep);
		}
		StringPrint(result->output_file, StringLen(result->output_file), L"%hs", pt);
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
		
		result->source_drive = StringPrint(NULL, 0, L"%ls%hs", strncmp(image_file, "\\\\.\\", 4) ? L"\\\\.\\" : L"", image_file);
		if (!VerifyVolumeInfo(result->source_drive, image_vol, &result->image_offs))
			return ErrorCleanUp(DeleteSettings, result, "");

		result->is_image = true;
	}

	if (!file_name_path || !*file_name_path)
	{
		return ErrorCleanUp(DeleteSettings, result, "Error: FileNamePath parameter not specified.\n");
	}

	char* tail = NULL;

	bool physical_drive = match("Harddisk\\d\\d*Partition\\d\\d*:", file_name_path, &tail) ||
		match("HarddiskVolume\\d\\d*:", file_name_path, &tail) ||
		match("HarddiskVolumeShadowCopy\\d\\d*:", file_name_path, &tail);

	if (!physical_drive && match("PhysicalDrive\\d\\d*:", file_name_path, &tail))
	{
		// Arriving here, means that we have something like 'PhysicalDrive[XYZ]:file'
		// In the "normal" case [XYZ] would simply be a number, eg 'PhysicalDrive1:\file'
		// Original rawcopy also accepts other cases, but treats them as images
		physical_drive = true;

		string image = StringPrint(NULL, 0, L"%ls%.*hs", strncmp(file_name_path, "\\\\.\\", 4) ? L"\\\\.\\" : L"",
			(size_t)(tail - file_name_path - 1), file_name_path);
		uint64_t vol_offset;
		wchar_t* vol = BaseString(image);
		if (!VerifyVolumeInfo(image, image_vol, &vol_offset))
		{
			DeleteString(image);
			return ErrorCleanUp(DeleteSettings, result, "Error: No NTFS found on physical drive.\n");
		}
		if (result->source_drive)
			DeleteString(result->source_drive);
		result->source_drive = image;
		result->image_offs = vol_offset;
	}

	if (physical_drive)
	{
		//It's a validated physical drive
		//Not much to do, just set source_drive (if needed) and one of 'mft_ref' or 'source path'
		result->is_image = false;

		if (!result->source_drive)
			result->source_drive = StringPrint(NULL, 0, L"%ls%.*hs", strncmp(file_name_path, "\\\\.\\", 4) ? L"\\\\.\\" : L"",
				(size_t)(tail - file_name_path - 1), file_name_path);

		if (IsDigits(tail))
		{
			SafeAlloc(result->mft_ref, 1);
			*(result->mft_ref) = strtoul(tail, NULL, 10);
		}
		else
			result->source_path = StringPrint(NULL, 0, L"%hs", tail);
	}
	else if (result->is_image && IsDigits(file_name_path + 2))
	{
		// Not a physical drive and a valid image: file_name_path should give
		// us either a path or an mft reference, which is a simple number, as
		// tested above
		SafeAlloc(result->mft_ref, 1);
		*(result->mft_ref) = strtoul(file_name_path + 2, NULL, 10);
	}
	else if (result->is_image)
	{
		// Not a physical drive and a valid image: file_name_path should give
		// a path.
		// So it should be 'FileNamePath:x:\...\...\...', in which we ignore
		// the 'x'
		// We do accpet 'FileNamePath:\...\...\...', which we'll patch up ourselves,
		// all the rest triggers an error, but not the original rawcopy error message.
		if (file_name_path[1] != ':' && file_name_path[0] != '\\')
			return ErrorCleanUp(DeleteSettings, result, "Incorrectly formatted file name path: %s\n", file_name_path);

		result->source_path = StringPrint(NULL, 0, L"%ls%hs", file_name_path[0] == '\\' ? L"x:" : L"", file_name_path);
	}
	else if (PathFileExistsA(file_name_path))
	{
		// This is the 'most normal' case: copy an existing file with the full path
		// specified, // ie '/FileNamePath:d:\...\...\...'
		// All we need to do is split 'file_name_path' in the drive part and the path
		result->source_drive = StringPrint(NULL, 0, L"%ls%.2hs", strncmp(file_name_path, "\\\\.\\", 4) ? L"\\\\.\\" : L"",
											file_name_path);
		result->source_path = StringPrint(NULL, 0, L"%hs", file_name_path);
	}
	else if (file_name_path[1] == ':' && IsDigits(file_name_path + 2))
	{
		// File not found, but it's the "mft reference scenario"
		// All we need to do is split 'file_name_path' in the drive part and the path
		result->source_drive = StringPrint(NULL, 0, L"%ls%.2hs", strncmp(file_name_path, "\\\\.\\", 4) ? L"\\\\.\\" : L"",
			file_name_path);
		SafeAlloc(result->mft_ref, 1);
		*(result->mft_ref) = strtoul(file_name_path + 2, NULL, 10);
	}
	else if (file_name_path[1] != ':')
		//Don't know what this is; in any case it's messed up:
		return ErrorCleanUp(DeleteSettings, result, "Incorrectly formatted file name path: %s\n", file_name_path);
	else
	{
		// Final option is a bit exotic: normal file path, correctly formatted, but 
		// Windows API (PathFileExistsA) can't find it: give a warning an treat it as
		// the normal case
		printf("Warning: File not found with regular file search: %s\n", file_name_path);
		result->source_drive = StringPrint(NULL, 0, L"%ls%.2hs", strncmp(file_name_path, "\\\\.\\", 4) ? L"\\\\.\\" : L"",
			file_name_path);
		result->source_path = StringPrint(NULL, 0, L"%hs", file_name_path);
	}

	return result;
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
		DeleteString(set->output_folder);

	if (set->output_file)
		DeleteString(set->output_file);

	if (set->source_drive)
		DeleteString(set->source_drive);

	if (set->source_path)
		DeleteString(set->source_path);

	free(set);
}

