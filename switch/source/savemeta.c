#include "savemeta.h"

#include <string.h>

static void put(uint8_t **p, uint64_t v, int n) {
    for (int i = 0; i < n; i++) *(*p)++ = (uint8_t)(v >> (8 * i));
}

static uint64_t get(const uint8_t **p, int n) {
    uint64_t v = 0;
    for (int i = 0; i < n; i++) v |= (uint64_t)(*(*p)++) << (8 * i);
    return v;
}

void savemeta_encode(const SaveMeta *m, uint8_t out[SAVE_META_SIZE]) {
    uint8_t *p = out;
    put(&p, m->magic, 4);
    put(&p, m->revision, 1);
    put(&p, m->application_id, 8);
    memcpy(p, m->account_id, 16);
    p += 16;
    put(&p, m->system_save_id, 8);
    put(&p, m->save_data_type, 1);
    put(&p, m->save_data_rank, 1);
    put(&p, m->save_data_index, 2);
    put(&p, m->owner_id, 8);
    put(&p, m->timestamp, 8);
    put(&p, m->flags, 4);
    put(&p, (uint64_t)m->save_data_size, 8);
    put(&p, (uint64_t)m->journal_size, 8);
    put(&p, m->commit_id, 8);
    put(&p, m->save_data_space_id, 1);
}

bool savemeta_decode(const uint8_t *data, size_t size, SaveMeta *m) {
    if (size < SAVE_META_SIZE) return false;
    const uint8_t *p = data;
    m->magic = (uint32_t)get(&p, 4);
    if (m->magic != SAVE_META_MAGIC) return false;
    m->revision = (uint8_t)get(&p, 1);
    m->application_id = get(&p, 8);
    memcpy(m->account_id, p, 16);
    p += 16;
    m->system_save_id = get(&p, 8);
    m->save_data_type = (uint8_t)get(&p, 1);
    m->save_data_rank = (uint8_t)get(&p, 1);
    m->save_data_index = (uint16_t)get(&p, 2);
    m->owner_id = get(&p, 8);
    m->timestamp = get(&p, 8);
    m->flags = (uint32_t)get(&p, 4);
    m->save_data_size = (int64_t)get(&p, 8);
    m->journal_size = (int64_t)get(&p, 8);
    m->commit_id = get(&p, 8);
    m->save_data_space_id = (uint8_t)get(&p, 1);
    return true;
}
