#include <math.h>
#include <stdlib.h>

#include "attribs.h"
#include "bootsector.h"
#include "fileio.h"
#include "helpers.h"



boot_sector ReadFromDisk(wchar_t* drive, uint64_t offset)
{
    bytes sector = ReadBytesFromFile(drive, offset, 512);

    if (!sector)
        return NULL;

    boot_sector result = (boot_sector)sector->buffer;

    free(sector);

    return result;
}

void PrintInformation(boot_sector sect, FILE* file)
{
    fprintf(file, "BytesPerSector: %ld\n", sect->bytes_per_sector);
    fprintf(file, "SectorsPerCluster: %ld\n", sect->sectors_per_clusters);
    fprintf(file, "SectorsPerTrack: %ld\n", sect->sectors_per_track);
    fprintf(file, "NumberOfHeads: %ld\n", sect->number_of_heads);
    fprintf(file, "TotalSectors: %lld\n", sect->total_sectors);
    fprintf(file, "LogicalClusterNumberforthefileMFT: %lld\n", sect->logical_clust_num_for_mft);
    fprintf(file, "LogicalClusterNumberforthefileMFTMirr: %lld\n", sect->logical_clust_num_for_mft_mir);
    fprintf(file, "MFTRecordSize: %ld\n", MFTRecordSize(sect));
}
