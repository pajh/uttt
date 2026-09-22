#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h> 
#include <emmintrin.h>

#define GAME_RES_WIDTH	384
#define GAME_RES_HEIGHT	240

const unsigned char marker[4] = {0xFF,0xFF,0xFF,0xFF};

uint64_t get_gtod_clock_time ()
{
    struct timeval tv;

    if (gettimeofday (&tv, NULL) == 0)
        return (uint64_t) (tv.tv_sec * 1000000 + tv.tv_usec);
    else
        return 0;
}

typedef struct PIXEL32_s {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
    unsigned char alpha;
} PIXEL32;


void test(char* memory, int size_32bs, int test_no) {

    int size_8bs = size_32bs * 4;
    int size_64bs = size_8bs / 8;
    int size_128bs = size_8bs / 16;
    int size_256bs = size_8bs / 32;

    if ( size_256bs * 32 != size_8bs) {
        fprintf(stderr,"Error\n");
        exit(0);
    }

    uint8_t r = 0xD0;
    uint8_t g = 0xC0;
    uint8_t b = 0xB0;
    uint8_t a = 0xA0;

    // pixels
    PIXEL32 pixel = {r,g,b,a};
    uint32_t pixel32 = r | g<<8 | b <<16 |a << 24;
    uint64_t pixel64 = (uint64_t)pixel32 | (uint64_t) pixel32 << 32;
    PIXEL32 pixel_a[8] = {pixel,pixel,pixel,pixel,pixel,pixel,pixel,pixel};

    //printf("[%x]\n",pixel32);
    
    //Clear mem
    memset( memory, 0x00, size_8bs );

    // various address casts for clarity
    uint32_t* mem32 = (uint32_t*)memory;
    uint64_t* mem64 = (uint64_t*)memory;
    __m128i* mem128 = (__m128i*)memory;

    uint64_t start, end;

    __m128i pixel128 = _mm_set1_epi32(pixel32);

    switch (test_no)
    {
    case 1:
        fprintf(stderr,"4 byte memcpy:");
        start=get_gtod_clock_time();
        for (int x = 0; x < size_32bs; x +=1 ) {
            memcpy( (PIXEL32*)memory+x, &pixel, sizeof(PIXEL32));
        }
        end=get_gtod_clock_time();
        break;

    case 2:
        fprintf(stderr,"4 byte u32 assign:");
        start=get_gtod_clock_time();
        for (int x = 0; x < size_32bs; x += 1) {
            *(mem32+x) = pixel32;    
        }
        end=get_gtod_clock_time();
        break;

    case 3:
        fprintf(stderr,"8 byte u64 assign:");
        start=get_gtod_clock_time();
        for (int x = 0; x < size_64bs; x += 1) {
            *(mem64+x) = pixel64;    
        }
        end=get_gtod_clock_time();
        break;

    case 4:
        fprintf(stderr,"128 bit simd store:");
        start=get_gtod_clock_time();
        for (int x = 0; x < size_128bs; x += 1) {
            _mm_store_si128(mem128+x, pixel128);
        }
        end=get_gtod_clock_time();
        break;

    case 5:
        fprintf(stderr,"16 byte memcpy:");
        start=get_gtod_clock_time();
        for (int x = 0; x < size_32bs; x +=4 ) {
            memcpy( (PIXEL32*)memory+x, pixel_a, sizeof(PIXEL32) * 4);
        }
        end=get_gtod_clock_time();
        break;

    case 6:
        fprintf(stderr,"32 byte memcpy:");
        start=get_gtod_clock_time();
        for (int x = 0; x < size_32bs; x +=8 ) {
            memcpy( (PIXEL32*)memory+x, pixel_a, sizeof(PIXEL32) * 8);
        }
        end=get_gtod_clock_time();
        break;

    default:
        fprintf(stderr,"No test run %d:", test_no);
        start = 0; end = 0;
        break;
    }

    for (int x = 0; x < size_32bs; x+=2) {
        //memcpy( (PIXEL32*)memory+x, &pixel, sizeof(PIXEL32));
        *(mem32+x) = pixel32;
        *(mem32+x+1) = pixel32;
    }
    

    if ( memcmp(memory+size_8bs, marker,4) != 0) {
        printf("Marker has been overwritten\n");
    }

    for (int x = 0; x < size_32bs; x++) {
        if ( memcmp( (PIXEL32*)memory+x, &pixel,4) != 0) {
            printf("memory incorrect at int=%d expected \n",x);
        }
    }

    uint64_t elapsed = end - start;
    printf(" %lu\n", elapsed);
    
}



int main(void)
{
    int size_32bs  = (GAME_RES_HEIGHT * GAME_RES_WIDTH);
    fprintf(stderr,"Screen is %d 4byte ints\n",size_32bs);
    int size_8bs =  size_32bs * 4;
    char* memory = malloc( size_8bs + 4);
    memcpy( ( memory+size_8bs ), marker,4 );  // Add marker
   
    test(memory, size_32bs, 1);
    test(memory, size_32bs, 2);
    test(memory, size_32bs, 3);
    test(memory, size_32bs, 4);
    test(memory, size_32bs, 5);
    test(memory, size_32bs, 6);

    free(memory);    
}