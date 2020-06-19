#ifndef MFT_H
#define MFT_H

#include "context.h"
#include "byte-buffer.h"
#include "fileio.h"
#include "ut-wrapper.h"


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

typedef struct _mft_record *mft_record;

bool SetUpMFTIndex(execution_context context);

bool SetUppercaseList(execution_context context);

UT_array* GetAttributes(uint32_t attributeType, const mft_record mft);

mft_record AttributesForFile(execution_context context, uint64_t mft_ref, uint32_t attribute_mask);

wchar_t* FileNameFromMFTRec(const mft_record mft);

void DeleteMFTRecord(mft_record rec);

bool DoFixUp(bytes record, uint16_t sector_sz);

#endif //MFT_H
