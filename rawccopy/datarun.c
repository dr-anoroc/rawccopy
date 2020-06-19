#include "datarun.h"
#include "helpers.h"

static const UT_icd run_icd = { sizeof(run), NULL, NULL, NULL };

void AppendRun(UT_array* list, uint32_t vcn, uint32_t Clusters);

void ParseRunlist(UT_array* dst, uint8_t* input, rsize_t count);

void ParseRunlist(UT_array* dst, uint8_t* input, rsize_t count)
{
    uint32_t tot_clust = 0;
    uint32_t base_vcn = 0;

    for (rsize_t i = 0; i < count && *(input + i);)
    {
        rsize_t clust_len = (rsize_t)(*(input + i) & 0x0F);
        rsize_t offset_len = (rsize_t)((*(input + i) & 0xF0) >> 4);
        i++;
        uint32_t num_clusters = (uint32_t)ParseUnsigned(input + i, clust_len);
        i += clust_len;

        int32_t this_vcn = (uint32_t)ParseSigned(input + i, offset_len);

        base_vcn += this_vcn;

        AppendRun(dst, !this_vcn ? 0 : base_vcn, num_clusters);

        i += offset_len;

    }
}

void ExtractRunsFromAttribute(UT_array* dst, const attribute attr)
{
    ParseRunlist(dst, (uint8_t*)attr + attr->run_list_offs, attr->length - attr->run_list_offs);
}

UT_array* EmptyRunList()
{
    UT_array* result;
    utarray_new(result, &run_icd);
    return result;
}

void AppendRun(UT_array* list, uint32_t vcn, uint32_t clusters)
{
    run new_run = { vcn, clusters};
    utarray_push_back(list, &new_run);
}

bytes BlockFromRunList(const cluster_reader cr, UT_array* run_list, int8_t block_fact, uint64_t block_index)
{
	rsize_t block_size = block_fact < 0 ? ((rsize_t)1 << -block_fact) : (rsize_t)block_fact * ClusterSize(cr);

	uint64_t byte_cnt = 0;

	run* start_run;
	uint64_t start_pos = 0;
	uint64_t run_bytes = 0;
	for (start_run = utarray_front(run_list); start_run; start_run = utarray_next(run_list, start_run))
	{
		run_bytes = (uint64_t)start_run->clusters * ClusterSize(cr);
		if ((byte_cnt + run_bytes) > block_index * block_size)
		{
			start_pos = block_index * block_size - byte_cnt;
			break;
		}
		byte_cnt += run_bytes;
	}
	if (!start_run)
		return NULL;

	//Now start reading bytes until we have a full block
	bytes result = CreateEmpty();
	for (; start_run && result->buffer_len < block_size; start_run = utarray_next(run_list, start_run))
	{
		rsize_t numbytes = (rsize_t)min(run_bytes - start_pos, block_size - result->buffer_len);
		bytes extra = ReadImageBytes(cr, (uint64_t)start_run->vcn * ClusterSize(cr) + start_pos, numbytes);
		Append(result, extra, 0, numbytes);
		DeleteBytes(extra);
	}

	if (result->buffer_len != block_size)
		return ErrorCleanUp(DeleteBytes, result, "");

	return result;
}
