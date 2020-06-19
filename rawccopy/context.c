
#include "context.h"
#include "mft.h"
#include "helpers.h"

execution_context SetupContext(int argc, char* argv[])
{
	SafeCreate(result, execution_context);
	memset(result, 0, sizeof(struct _execution_context));

	if (!((result->parameters = Parse(argc, argv))))
		return ErrorCleanUp(free, result, "");

	if (!(result->boot = ReadFromDisk(result->parameters->target_drive, result->parameters->image_offs)))
		return ErrorCleanUp(CleanUp, result, "");

	if (result->parameters->write_boot_info)
	{
		wchar_t* buffer = JoinStrings(L"\\", result->parameters->output_folder, L"VolInfo.txt", NULL);
		FILE* f;
		if (_wfopen_s(&f, buffer, L"w+"))
		{
			PrintInformation(result->boot, f);
			if (f) fclose(f);
		}
		free(buffer);
	}

	if (!(result->cr = OpenClusterReader(result->parameters->target_drive, result->parameters->image_offs,
						BytesPerCluster(result->boot))))
		return ErrorCleanUp(CleanUp, result, "");

	if (!SetUpMFTIndex(result))
		return ErrorCleanUp(CleanUp, result, "");

	if (!SetUppercaseList(result))
		return ErrorCleanUp(CleanUp, result, "");

	return result;
}

bool WriteDataToDestination(execution_context context, const wchar_t* file, const bytes data)
{
	wchar_t* output_path = NULL;
	bool result = true;

	if (context->parameters->tcp_send)
	{
		//It's TCP, check if it's already initialised
		if (!context->writer)
		{
			if (!(context->writer = TCPWriter(context->parameters->ip_addr, context->parameters->tcp_port)))
				result = false;
			else
				wprintf(L"Tcpsending: %ls\n", file);
		}
	}
	//It's a file:
	else
	{
		wchar_t* output_path = JoinStrings(L"\\", context->parameters->output_folder, file, NULL);
		if (!context->writer)
		{
			//If it's not itialised, try to do so
			if (!(context->writer = FileWriter(output_path)))
				result = false;
			else
				wprintf(L"Writing: %ls\n", file);
		}
		else if (wstrncmp_nocase(CurrentFileName(context->writer), output_path, 0xffff))
		{
			//It was already initialised, but under a different name:
			//Close the existing one and try to open a new one, with the new name
			CloseDataWriter(context->writer);
			if (!(context->writer = FileWriter(output_path)))
				result = false;
			else
				wprintf(L"Writing: %ls\n", file);
		}
		free(output_path);
	}
	if (result)
	{
		result = WriteData(context->writer, data);
	}
	return result;
}

void CleanUp(execution_context context)
{
	if (context->boot)
		free(context->boot);

	if (context->parameters)
		DeleteSettings(context->parameters);

	if (context->mft_index)
		utarray_free(context->mft_index);

	if (context->cr)
		CloseClusterReader(context->cr);

	if (context->writer)
		CloseDataWriter(context->writer);

	if (context->upper_case)
		free(context->upper_case);


	free(context);
}
