#ifndef DATARUN_H
#define DATARUN_H

#include <stdint.h>
#include "attribs.h"
#include "fileio.h"
//#include "ut-wrapper.h"
//#include "byte-buffer.h"

typedef struct _run
{
    uint32_t vcn;
    uint32_t clusters;
} run;


bytes BlockFromRunList(const cluster_reader cr, UT_array* run_list, int8_t block_fact, uint64_t block_index);

void ExtractRunsFromAttribute(UT_array *dst, const attribute attr);

UT_array* EmptyRunList();

#endif DATARUN_H
