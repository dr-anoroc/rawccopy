#ifndef BOOTSECTOR_H
#define BOOTSECTOR_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>


#pragma pack (push, 1)
struct _boot_sector
{
    uint8_t jump[3];                            // Irrelevant (jump to boot up code).
    uint8_t oem_id[8];                          // Magic "NTFS    ".
    uint16_t bytes_per_sector;
    uint8_t sectors_per_clusters;
    uint16_t reserved_clusters;
    uint8_t  fats;                              // zero 
    uint16_t root_entries;                      // zero 
    uint16_t sectors;                           // zero 
    uint8_t  media_type;                        // 0xf8 = hard disk 
    uint16_t sectors_per_fat;                   // zero 
    uint16_t sectors_per_track;                 // irrelevant 
    uint16_t number_of_heads;                   // irrelevant 
    uint32_t hidden_sectors;                    // zero 
    uint32_t large_sectors;                     // zero 
    uint8_t  unused[4];                         // zero, NTFS diskedit.exe states that this is actually:
                                                //      __uint8_t physical_drive;    // 0x80
                                                //      __uint8_t current_head;  // zero
                                                //      __uint8_t extended_boot_signature; // 0x80
                                                //      __uint8_t unused;        // zero
    uint64_t total_sectors;                     // Number of sectors in volume. Gives maximum volume size of 2^63 sectors.
                                                // Assuming standard sector size of 512 bytes, the maximum byte size is
                                                // approx. 4.7x10^21 bytes. (-;
    uint64_t logical_clust_num_for_mft;         // Cluster location of mft data.
    uint64_t logical_clust_num_for_mft_mir;     // Cluster location of copy of mft.
    int8_t clusters_per_file_record;            // Mft record size in clusters. 
    uint8_t  reserved0[3];                      // zero 
    int8_t  clusters_per_index_record;          // Index block size in clusters.
    uint8_t  reserved1[3];                      // zero
    uint64_t volume_serial_nbr;                 // Irrelevant (serial number).
    uint32_t checksum;                          // Boot sector checksum.
    uint8_t  bootstrap[426];                    // Irrelevant (boot up code).
    uint16_t end_of_sector;
};
#pragma pack(pop)

typedef struct _boot_sector *boot_sector;

inline uint32_t BytesPerCluster(boot_sector boot) { return ((uint32_t)boot->bytes_per_sector * (uint32_t)boot->sectors_per_clusters); };

inline uint32_t MFTRecordSize(boot_sector boot)
{
    return boot->clusters_per_file_record < 0 ? (1L << (256 - (uint32_t)boot->clusters_per_file_record)) :
        BytesPerCluster(boot) * (uint32_t)boot->clusters_per_file_record;
}

boot_sector ReadFromDisk(wchar_t* drive, uint64_t offset);

void PrintInformation(boot_sector sect, FILE* file);

#endif