#include "save_fingerprint.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sha256.h"

#define READ_CHUNK_BYTES (64 * 1024)

static bool hash_file(const char *root, const char *rel_path, unsigned char *buffer, char out[SHA256_HEX_SIZE])
{
    char full_path[2 * TS_PATH_SIZE + 64];
    save_tree_full_path(root, rel_path, full_path, sizeof full_path);
    FILE *file = fopen(full_path, "rb");
    if (file == NULL) {
        return false;
    }
    sha256_t hash;
    sha256_init(&hash);
    size_t got;
    while ((got = fread(buffer, 1, READ_CHUNK_BYTES, file)) > 0) {
        sha256_update(&hash, buffer, got);
    }
    bool is_ok = ferror(file) == 0;
    fclose(file);
    unsigned char digest[SHA256_DIGEST_SIZE];
    sha256_final(&hash, digest);
    sha256_to_hex(digest, out);
    return is_ok;
}

static tree_status_t hash_files(const char *root, const save_tree_t *tree, sha256_t *outer, char *problem_path,
                                size_t problem_size)
{
    unsigned char *buffer = malloc(READ_CHUNK_BYTES);
    if (buffer == NULL) {
        return TREE_ERR_MEMORY;
    }
    tree_status_t status = TREE_OK;
    for (int i = 0; i < tree->count && status == TREE_OK; i++) {
        const save_tree_entry_t *entry = &tree->items[i];
        char content_hash[SHA256_HEX_SIZE];
        if (entry->is_directory) {
            continue;
        }
        if (!hash_file(root, entry->path, buffer, content_hash)) {
            snprintf(problem_path, problem_size, "%s", entry->path);
            status = TREE_ERR_READ;
            break;
        }
        sha256_update(outer, entry->path, strlen(entry->path));
        sha256_update(outer, "\n", 1);
        sha256_update(outer, content_hash, SHA256_HEX_SIZE - 1);
        sha256_update(outer, "\n", 1);
    }
    free(buffer);
    return status;
}

tree_status_t save_fingerprint_dir(const char *root, char out[SAVE_FINGERPRINT_SIZE], char *problem_path,
                                   size_t problem_size)
{
    save_tree_t tree;
    out[0] = '\0';
    problem_path[0] = '\0';
    tree_status_t status = save_tree_scan(root, &tree);
    if (status != TREE_OK) {
        snprintf(problem_path, problem_size, "%s", tree.problem_path);
        save_tree_free(&tree);
        return status;
    }
    sha256_t outer;
    sha256_init(&outer);
    status = hash_files(root, &tree, &outer, problem_path, problem_size);
    save_tree_free(&tree);
    if (status != TREE_OK) {
        return status;
    }
    unsigned char digest[SHA256_DIGEST_SIZE];
    char hex[SHA256_HEX_SIZE];
    sha256_final(&outer, digest);
    sha256_to_hex(digest, hex);
    snprintf(out, SAVE_FINGERPRINT_SIZE, "sha256:%s", hex);
    return TREE_OK;
}
