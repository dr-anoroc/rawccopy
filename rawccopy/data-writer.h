#ifndef DATA_WRITER_H
#define DATA_WRITER_H

#include "byte-buffer.h"
#include "safe-string.h"

typedef struct _data_writer* data_writer;

data_writer TCPWriter(uint32_t ip, uint16_t port);

data_writer FileWriter(const string file_name);

void CloseDataWriter(data_writer writer);

bool WriteData(const data_writer wr, const bytes data);

#endif DATA_WRITER_H
