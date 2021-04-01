#ifndef MFT_H
#define MFT_H

#include "context.h"
#include "byte-buffer.h"
#include "fileio.h"
#include "ut-wrapper.h"
#include "attribs.h"


#define MASTER_FILE_TABLE_NUMBER         (0x0)   //  $Mft
#define MASTER_FILE_TABLE2_NUMBER        (0x1)   //  $MftMirr
#define LOG_FILE_NUMBER                  (0x2)   //  $LogFile
#define VOLUME_DASD_NUMBER               (0x3)   //  $Volume
#define ATTRIBUTE_DEF_TABLE_NUMBER       (0x4)   //  $AttrDef
#define ROOT_FILE_NAME_INDEX_NUMBER      (0x5)   //  .
#define BIT_MAP_FILE_NUMBER              (0x6)   //  $BitMap
#define BOOT_FILE_NUMBER                 (0x7)   //  $Boot
#define BAD_CLUSTER_FILE_NUMBER          (0x8)   //  $BadClus
#define QUOTA_TABLE_NUMBER               (0x9)   //  $Quota
#define UPCASE_TABLE_NUMBER              (0xa)   //  $UpCase


typedef struct _mft_file *mft_file;

typedef struct _attribute_reader* attribute_reader;

mft_file LoadMFTFile(execution_context context, uint64_t index);

void DeleteMFTFile(mft_file file);

attribute FirstAttribute(execution_context context, mft_file mft_rec, uint32_t attribute_mask);

attribute NextAttribute(execution_context context, mft_file mft_rec, const attribute cur, uint32_t attribute_mask);

attribute_reader OpenAttributeReader(execution_context context, mft_file mft_rec, const attribute attribute);

void CloseAttributeReader(attribute_reader rdr);

bool AppendBytesFromAttribRdr(execution_context context, attribute_reader rdr, int64_t offset, uint64_t cnt, bytes dest, rsize_t pos);

bytes GetBytesFromAttribRdr(execution_context context, attribute_reader rdr, int64_t offset, uint64_t cnt);

bytes GetBytesFromAttrib(execution_context context, mft_file mft_rec, const attribute attrib, int64_t offset, uint64_t cnt);

bool DoFixUp(bytes record, uint16_t sector_sz);

#endif //MFT_H
