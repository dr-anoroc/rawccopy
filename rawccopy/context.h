#ifndef CONTEXT_H
#define CONTEXT_H

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#include "settings.h"
#include "bootsector.h"
#include "fileio.h"
#include "data-writer.h"
#include "ut-wrapper.h"

struct _execution_context
{
	settings parameters;
	boot_sector boot;
	UT_array* mft_index;
	cluster_reader cr;
	data_writer writer;
	wchar_t *upper_case;
};

typedef struct _execution_context* execution_context;

execution_context SetupContext(int argc, char* argv[]);

bool WriteDataToDestination(execution_context context, const wchar_t* file, const bytes data);

void CleanUp(execution_context context);

#endif //CONTEXT_H
