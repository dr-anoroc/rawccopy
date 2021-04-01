#include <math.h>
#include <stdlib.h>

#include "attribs.h"
#include "disk-info.h"
#include "fileio.h"
#include "helpers.h"

#pragma pack (push, 1)
typedef struct {
    /* 0x00 */	uint8_t	status;         /*  Status or physical drive (bit 7 set is for active or bootable, old MBRs only accept
                                            0x80, 0x00 means inactive, and 0x01–0x7F stand for invalid) */
    /* 0x01 */	uint8_t	start_CHS[3];   /*  Address of first absolute sector in partition, described in three bytes. */
    /* 0x04 */	uint8_t	type;           /*  Partition type */
    /* 0x05 */	uint8_t	end_CHS[3];     /*  Address of last absolute sector in partition, described in three bytes. */
    /* 0x08 */	uint32_t first_LBA;     /*  LBA of first absolute sector in the partition */
    /* 0x0c */	uint32_t sector_cnt;    /*  Number of sectors in partition */
} partition_info;
#pragma pack(pop)

#pragma pack (push, 1)
typedef struct _mbr_record {
    /* 0x000 */ uint8_t bootstrap[0x1b8];
    /* 0x1b8 */	uint32_t signature;             /*  Bit disk signature	Disk signature(optional; UEFI, Linux, Windows NT family and other OSes) */
    /* 0x1bc */	uint16_t copy_protect;          /*  0x0000 (0x5A5A if copy - protected) */
    /* 0x1be */	partition_info partitions[4];   /*  Partition table */
    /* 0x1fe */	uint16_t boot_signatures;       /*  Boot signature */
} *mbr_record;
#pragma pack(pop)


#pragma pack (push, 1)
typedef struct _GPT_hdr {
    /* 0x00 */	unsigned char signature[8];   /* Signature("EFI PART", 45h 46h 49h 20h 50h 41h 52h 54h)* /
    /* 0x08 */	unsigned char revision[4];    /*  Revision 1.0 (00h 00h 01h 00h) for UEFI 2.8 */
    /* 0x0c */	uint32_t hdr_sz;              /*  Header size (in bytes, usually 5Ch 00h 00h 00h or 92 bytes */
    /* 0x10 */	uint32_t hdr_crc;             /*  CRC32 of header(offset + 0 up to header size) */
    /* 0x14 */	unsigned char reserved[4];    /*  Reserved; must be zero */
    /* 0x18 */	uint64_t current_lba;         /*  Current LBA (location of this header copy) */
    /* 0x20 */	uint64_t backup_lba;          /*  Backup LBA (location of the other header copy) */
    /* 0x28 */	uint64_t first_avail_lba;     /*  First usable LBA for partitions (primary partition table last LBA + 1) */
    /* 0x30 */	uint64_t last_avail_lba;      /*  Last usable LBA (secondary partition table first LBA - 1) */
    /* 0x38 */	unsigned char guid[16];       /*  Disk GUID */
    /* 0x48 */	uint64_t start_lba;           /*  Starting LBA of array of partition entries (always 2 in primary copy) */
    /* 0x50 */	uint32_t entry_cnt;           /*  Number of partition entries in array) */
    /* 0x54 */	uint32_t entry_size;          /*  Size of a single partition entry(usually 80h or 128) */
    /* 0x58 */	uint32_t entry_crc;           /*  CRC32 of partition entries array in little endian */
    /* 0x5c */	uint8_t reserved_block[420];  /*  Reserved; must be zeroes for the rest of the block */
} *GPT_hdr;
#pragma pack(pop)

#pragma pack (push, 1)
typedef struct _GPT_entry {
    /* 0x00 */	unsigned char type_guid[16];       /*  Partition type GUID */
    /* 0x10 */	unsigned char partition_guid[16];  /*  Unique partition GUID */
    /* 0x20 */	uint64_t start_lba;                /*  First LBA */
    /* 0x28 */	uint64_t end_lba;                  /*  Last LBA */
    /* 0x30 */	uint64_t att_flags;                /*  Attribute flags */
    /* 0x38 */	uint8_t partition_name[72];        /*  Partition name (36 UTF - 16LE code units) */
} *GPT_entry;
#pragma pack(pop)


typedef struct 
{
    bool has_ntfs;
    uint64_t start_sector;
    uint64_t sector_cnt;
} volume;

const UT_icd vol_icd = { sizeof(volume), NULL, NULL, NULL };

const char* null_part = "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";
const uint16_t sector_sz = 0x200;

UT_array* GPTVolumes(disk_reader dr);

UT_array* ExtPartitionVolumes(disk_reader dr, mbr_record first);

bool ReadNTFSBootSector(disk_reader dr, uint64_t offset, bytes dest);

bool VerifyVolumeInfo(const string volume_name, uint64_t index, uint64_t* volume_offset)
{
    disk_reader dr = OpenDiskReader(volume_name, sector_sz);
    if (!dr)
        return false;

    bytes mbr = GetBytesFromDiskRdr(dr, 0, sizeof(struct _mbr_record));
    if (!mbr)
        return CleanUpAndFail(CloseDiskReader, dr, "");

    partition_info* part1 = &(TYPE_CAST(mbr, mbr_record)->partitions[0]);
    UT_array *vols = NULL;

    bytes read_buffer = CreateEmpty();
    //First check for GPT or extended partitions:
    if (part1->type == 0xEE && part1->first_LBA == 1 && part1->sector_cnt == MAXUINT32)
        vols = GPTVolumes(dr);
    else if (part1->type == 0x05 || part1->type == 0x0F)
        vols = ExtPartitionVolumes(dr, TYPE_CAST(mbr, mbr_record));
    else if (!memcmp(part1, null_part, 16))
    {
        utarray_new(vols, &vol_icd);
        partition_info *part = part1;
        do
        {
            volume vol = { ReadNTFSBootSector(dr, (uint64_t)part->first_LBA * sector_sz, read_buffer),
                                    part->first_LBA, part->sector_cnt };
            utarray_push_back(vols, &vol);
        } while (!memcmp(++part, null_part, 16));
    }
    else if (ReadNTFSBootSector(dr, 0, read_buffer))
    {
        utarray_new(vols, &vol_icd);
        volume vol = { true, 0, TYPE_CAST(read_buffer, boot_sector)->total_sectors };
        utarray_push_back(vols, &vol);
    }

    bool result = true;
    bool any_ntfs = false;
    for (volume* v = vols ? utarray_front(vols) : NULL; v && !any_ntfs; v = utarray_next(vols, v))
        any_ntfs |= v->has_ntfs;

    if (!any_ntfs)
    {
        printf("Sorry, no NTFS volume found in that file.\n");
        result = false;
    }
    else
    {
        volume* res = utarray_eltptr(vols, index - 1);
        if (!(result = (res != NULL)))
        {
            printf("Error: Volume %lld does not exist in image.\n", index);
            printf("Found volumes are:\n");
            int i = 1;
            for (volume* v = utarray_front(vols); v; v = utarray_next(vols, v))
                wprintf(L"Volume %d, StartOffset %lld, Size %.2lf GB\n", i++, v->start_sector,
                    (double)((double)(v->sector_cnt) / 2.0 / 1024.0 / 1024.0));
        }
        else if (res && !res->has_ntfs)
        {
            printf("Error: Volume %lld does not contain an NTFS partition.\n", index);
            result = false;
        }
        else
            *volume_offset = res->start_sector * sector_sz;
    }

    if (vols)
        utarray_free(vols);

    DeleteBytes(read_buffer);
    CloseDiskReader(dr);
    DeleteBytes(mbr);
    return result;
}

UT_array* GPTVolumes(disk_reader dr)
{
    bytes gpt = GetBytesFromDiskRdr(dr, sector_sz, sector_sz);
    if (!gpt)
        return NULL;

    if (strcmp(TYPE_CAST(gpt, GPT_hdr)->signature, "EFI PART"))
        return ErrorCleanUp(DeleteBytes, gpt, "Error: Could not find GPT signature: %.*s\n", 8, TYPE_CAST(gpt, GPT_hdr)->signature);

    UT_array* vols;
    utarray_new(vols, &vol_icd);

    bytes entries = GetBytesFromDiskRdr(dr, TYPE_CAST(gpt, GPT_hdr)->start_lba * sector_sz, 
        (uint64_t)TYPE_CAST(gpt, GPT_hdr)->entry_cnt * TYPE_CAST(gpt, GPT_hdr)->entry_size);

    bytes ntfs_boot = CreateEmpty();

    if (entries)
    {   
        GPT_entry ent = TYPE_CAST(entries, GPT_entry);
        for (unsigned i = 0; i < TYPE_CAST(gpt, GPT_hdr)->entry_cnt; ++i)
        {
            volume vol = { ReadNTFSBootSector(dr, ent->start_lba * sector_sz, ntfs_boot),
                                    ent->start_lba, ent->end_lba - ent->start_lba};
            utarray_push_back(vols, &vol);
            ent++;
            if (ent->start_lba == 0 && ent->end_lba == 0)
                break;
        }
    }
    DeleteBytes(ntfs_boot);
    DeleteBytes(gpt);
    DeleteBytes(entries);
    return vols;
}

UT_array* ExtPartitionVolumes(disk_reader dr, mbr_record first)
{
    UT_array* vols;
    utarray_new(vols, &vol_icd);

    bytes ntfs_boot = CreateEmpty();
    bytes next_ebr = CreateEmpty();
    for (mbr_record cur = first; ; )
    {
        volume vol = { ReadNTFSBootSector(dr, (uint64_t)cur->partitions[1].first_LBA * sector_sz, ntfs_boot),
                            cur->partitions[1].first_LBA, cur->partitions[1].sector_cnt };
        utarray_push_back(vols, &vol);

        if (!memcmp(&cur->partitions[2], null_part, 16))
            break;
        else
        {
            AppendBytesFromDiskRdr(dr, ((uint64_t)cur->partitions[1].first_LBA + cur->partitions[2].first_LBA) * sector_sz,
                sector_sz, next_ebr, 0);
            cur = TYPE_CAST(next_ebr, mbr_record);
        }
    }

    DeleteBytes(ntfs_boot);
    DeleteBytes(next_ebr);
    return vols;
}

boot_sector ReadFromDisk(const string drive, uint64_t offset)
{
    boot_sector result = NULL;

    disk_reader dr = OpenDiskReader(drive, sector_sz);
    if (!dr)
        return NULL;

    bytes sector = CreateEmpty();

    if (!ReadNTFSBootSector(dr, offset, sector))
        return ErrorCleanUp(DeleteBytes, sector, "");   

    result = TYPE_CAST(sector, boot_sector);
    free(sector);

    CloseDiskReader(dr);

    return result;
}

bool ReadNTFSBootSector(disk_reader dr, uint64_t offset, bytes dest)
{
    if (!AppendBytesFromDiskRdr(dr, offset, sector_sz, dest, 0))
        return false;

    return strncmp(TYPE_CAST(dest, boot_sector)->oem_id, "NTFS", 4) == 0;
}

void PrintInformation(const boot_sector sect, FILE* file)
{
    fprintf(file, "BytesPerSector: %ld\n", sect->bytes_per_sector);
    fprintf(file, "SectorsPerCluster: %ld\n", sect->sectors_per_cluster);
    fprintf(file, "SectorsPerTrack: %ld\n", sect->sectors_per_track);
    fprintf(file, "NumberOfHeads: %ld\n", sect->number_of_heads);
    fprintf(file, "TotalSectors: %lld\n", sect->total_sectors);
    fprintf(file, "LogicalClusterNumberforthefileMFT: %lld\n", sect->logical_clust_num_for_mft);
    fprintf(file, "LogicalClusterNumberforthefileMFTMirr: %lld\n", sect->logical_clust_num_for_mft_mir);
    fprintf(file, "MFTRecordSize: %ld\n", sect->bytes_per_sector * sect->sectors);
}
