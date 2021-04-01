#ifndef CONTEXT_H
#define CONTEXT_H

#include "settings.h"
#include "disk-info.h"
#include "fileio.h"
#include "data-writer.h"
#include "ut-wrapper.h"

typedef struct _mft_file* mft_file;

typedef struct _execution_context
{
	settings parameters;
	boot_sector boot;
	uint32_t cluster_sz;
	uint32_t mft_record_sz;
	mft_file mft_table;
	disk_reader dr;
	data_writer writer;
	wchar_t *upper_case;
}*execution_context;


execution_context SetupContext(int argc, char* argv[]);

void CleanUp(execution_context context);

#endif //CONTEXT_H
