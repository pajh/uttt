
#include <stdio.h>
#include "hashmap.h"


void try_find(HashMap* hm, char * key, u32* data) {

    if ( findHMEntry(hm, (unsigned char*) key, data) ) {
        printf("Found [%s]=%d\n", key, *data);
    } else {
        printf("Not Found [%s]\n", key);
    }

}


void printkey(unsigned char *key) {
    printf("[");
    for (int i=0;i<10;i++) {
        printf("%c", (char) key[i]);
    }
    printf("]\n");
}

int main() {

    char* k1 = "12345678901234567890";
    char* k2 = "22345678908272635150";
    
    HashMap* hm = createHM(1000,3000);
    printf("Created HashMap\n");

    u32 data = 0;

    try_find(hm, k1, &data);

    data = 150202u;
    addHMEntry(hm, (unsigned char *)k1, data);
    printf("Inserted [%s] = %d\n", k1, data);

    //HMEntry* entry = hm->bucket_buffer[698];
    //printf("data=%d\n",entry->data);
    //printf("key = ");
    //printkey(entry->key);


    try_find(hm, k1, &data);
    data = 199011;
    addHMEntry(hm, (unsigned char *)k2, data);
    printf("Inserted [%s] = %d\n", k2, data);
    try_find(hm, k1, &data);
    try_find(hm, k2, &data);

    destroyHM(hm);

    return 1;

}