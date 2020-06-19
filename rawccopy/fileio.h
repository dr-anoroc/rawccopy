#ifndef FILEIO_H
#define FILEIO_H
#include <stdint.h>

#include "byte-buffer.h"

typedef struct _file_reader* file_reader;

typedef struct _cluster_reader* cluster_reader;

cluster_reader OpenClusterReader(const wchar_t* file_name, uint64_t image_offs, rsize_t cluster_sz);

rsize_t ClusterSize(const cluster_reader cr);

void CloseClusterReader(cluster_reader cr);

bytes ReadClusters(const cluster_reader cr, uint32_t cluster_offs, uint32_t cluster_cnt);

bytes ReadNextClusters(const cluster_reader cr, uint32_t cluster_cnt);

bool ReadNextClustersIn(const cluster_reader cr, uint32_t cluster_cnt, bytes dst);

bool ReadClustersIn(const cluster_reader cr, uint64_t cluster_offs, uint32_t cluster_cnt, bytes dst);

void SetNextCluster(const cluster_reader cr, uint64_t cluster_offs);

bytes ReadImageBytes(const cluster_reader cr, uint64_t byte_offs, uint64_t byte_cnt);

file_reader OpenFileReader(const wchar_t* file_name);

void CloseFileReader(file_reader fr);

void SetOffset(const file_reader fr, uint64_t offset);

bytes ReadNextBytes(const file_reader fr, uint64_t byte_cnt);

bytes ReadBytes(const file_reader fr, uint64_t offset, uint64_t byte_cnt);

bytes ReadBytesFromFile(const wchar_t* file_name, uint64_t offset, uint64_t byte_cnt);

bool ReadNextBytesIn(bytes dest, const file_reader fr, uint64_t byte_cnt);

bool ReadBytesIn(bytes dest, const file_reader fr, uint64_t offset, uint64_t byte_cnt);

#endif FILEIO_H

