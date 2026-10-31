// JKSV ".nx_save_meta.bin" (86 bytes, packed little-endian).
// Layout taken from JKSV include/fs/SaveMetaData.hpp:
//   u32 magic 'JKSV' | u8 revision | u64 applicationID | u8[16] accountID |
//   u64 systemSaveID | u8 saveDataType | u8 saveDataRank | u16 saveDataIndex |
//   u64 ownerID | u64 timestamp | u32 flags | s64 saveDataSize |
//   s64 journalSize | u64 commitID | u8 saveDataSpaceID
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SAVE_META_NAME ".nx_save_meta.bin"
#define SAVE_META_SIZE 86
#define SAVE_META_MAGIC 0x56534B4Au  // "JKSV" read as LE u32

#define TOTK_TITLE_ID 0x0100F2C0115B6000ULL

typedef struct {
    uint32_t magic;
    uint8_t revision;
    uint64_t application_id;
    uint8_t account_id[16];
    uint64_t system_save_id;
    uint8_t save_data_type;
    uint8_t save_data_rank;
    uint16_t save_data_index;
    uint64_t owner_id;
    uint64_t timestamp;
    uint32_t flags;
    int64_t save_data_size;
    int64_t journal_size;
    uint64_t commit_id;
    uint8_t save_data_space_id;
} SaveMeta;

void savemeta_encode(const SaveMeta *m, uint8_t out[SAVE_META_SIZE]);
// Returns false if size/magic is wrong.
bool savemeta_decode(const uint8_t *data, size_t size, SaveMeta *m);
