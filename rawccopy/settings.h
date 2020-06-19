#ifndef SETTINGS_H
#define SETTINGS_H
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "Shlwapi.h"

#include "helpers.h"

struct _settings {
	bool is_phys_drv;
	bool is_image;
	bool tcp_send;
	wchar_t ip_addr[MAX_LEN];
	wchar_t tcp_port[MAX_LEN];
	bool is_folder;
	unsigned int detail_mode;
	bool write_boot_info;
	bool all_attribs;
	wchar_t *output_file;
	wchar_t *output_folder;
	wchar_t *target_path;
	wchar_t *target_drive;
	uint64_t image_offs;
	unsigned int* mft_ref;
};

typedef struct _settings* settings;

settings Parse(int argc, char* argv[]);

void DeleteSettings(settings set);


#endif SETTINGS_H