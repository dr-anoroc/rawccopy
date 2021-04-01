#ifndef SETTINGS_H
#define SETTINGS_H
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "Shlwapi.h"

#include "helpers.h"
#include "safe-string.h"

typedef struct _settings {
	bool is_image;
	bool tcp_send;
	uint32_t ip_address;
	uint16_t tcp_port;
	unsigned int detail_mode;
	bool write_boot_info;
	bool all_attribs;
	string output_file;
	string output_folder;
	string source_path;
	string source_drive;
	uint64_t image_offs;
	uint64_t* mft_ref;
} *settings;

settings Parse(int argc, char* argv[]);

void DeleteSettings(settings set);


#endif SETTINGS_H