#ifndef DATA_WRITER_H
#define DATA_WRITER_H

#include "byte-buffer.h"

typedef struct _data_writer* data_writer;

data_writer TCPWriter(const wchar_t *ip, const wchar_t *port);

data_writer FileWriter(const wchar_t* file_name);

wchar_t* CurrentFileName(const data_writer writer);

void CloseDataWriter(data_writer writer);

bool WriteData(const data_writer wr, const bytes data);

#endif DATA_WRITER_H
