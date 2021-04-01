#ifndef FILEIO_H
#define FILEIO_H
#include <stdint.h>

#include "safe-string.h"
#include "byte-buffer.h"


typedef struct _disk_reader *disk_reader;

disk_reader OpenDiskReader(const string file_name, uint16_t sector_sz);

void CloseDiskReader(disk_reader dr);

bool AppendBytesFromDiskRdr(disk_reader dr, int64_t offset, uint64_t cnt, bytes dest, uint64_t pos);

bytes GetBytesFromDiskRdr(disk_reader dr, int64_t offset, uint64_t cnt);

#endif FILEIO_H

