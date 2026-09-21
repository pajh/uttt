/*
 * board2 blob/hash test runner.
 *
 * For each registered vector: allocate the output blob, run the vector's
 * build function, hash the result, and compare it with the expected hash.
 * A match prints PASS and frees the blob.  A mismatch dumps the blob under
 * work/test-failures/, reports the failure, and halts the whole run.
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "board2_test.h"

static void fail_vector(const Board2TestVector *vector, uint64_t actual,
                        const uint8_t *blob)
{
    char path[256];

    snprintf(path, sizeof path, "work/test-failures/%s.bin", vector->name);
    if (!test_save_blob(path, blob, vector->blob_len)) {
        fprintf(stderr, "FAIL %s: could not dump blob to %s\n",
                vector->name, path);
    } else {
        fprintf(stderr, "FAIL %s: dumped %zu bytes to %s\n",
                vector->name, vector->blob_len, path);
    }
    fprintf(stderr,
            "FAIL %s: hash=%016" PRIx64 " expected=%016" PRIx64 "\n",
            vector->name, actual, vector->expected_hash);
    exit(EXIT_FAILURE);
}

static void run_vector(const Board2TestVector *vector)
{
    uint8_t *blob = malloc(vector->blob_len);

    if (blob == NULL) {
        fprintf(stderr, "FAIL %s: could not allocate %zu bytes\n",
                vector->name, vector->blob_len);
        exit(EXIT_FAILURE);
    }

    vector->build(blob);
    uint64_t actual = test_hash64(blob, vector->blob_len);

    if (actual != vector->expected_hash)
        fail_vector(vector, actual, blob);

    free(blob);
}

int main(void)
{
    for (size_t i = 0; i < BOARD2_TEST_VECTOR_COUNT; i++)
        run_vector(&board2_test_vectors[i]);

    printf("board2 vectors: %zu/%zu passed\n",
           BOARD2_TEST_VECTOR_COUNT, BOARD2_TEST_VECTOR_COUNT);
    return EXIT_SUCCESS;
}
