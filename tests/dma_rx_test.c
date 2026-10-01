#include "uart_dma_rx.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t dma[256]; static UartRing ring; static UartDmaCursor cursor;
static void expect(unsigned first, unsigned count) {
    uint8_t byte;
    for(unsigned n=0;n<count;++n) { assert(uart_ring_pop(&ring,&byte)); assert(byte==(uint8_t)(first+n)); }
    assert(!uart_ring_pop(&ring,&byte));
}
int main(void) {
    for(unsigned n=0;n<256;++n) dma[n]=(uint8_t)n;
    assert(uart_dma_drain(&cursor,dma,256,17,&ring)==17); expect(0,17);
    assert(uart_dma_drain(&cursor,dma,256,17,&ring)==0);
    assert(uart_dma_drain(&cursor,dma,256,128,&ring)==111); expect(17,111);
    assert(uart_dma_drain(&cursor,dma,256,256,&ring)==128); expect(128,128);
    assert(uart_dma_drain(&cursor,dma,256,0,&ring)==0);
    assert(uart_dma_drain(&cursor,dma,256,250,&ring)==250); expect(0,250);
    assert(uart_dma_drain(&cursor,dma,256,12,&ring)==18); expect(250,18);
    memset(&cursor,0,sizeof cursor); memset(&ring,0,sizeof ring);
    /* Frame boundaries must not matter to the byte window layer. */
    const char *frames="S,10,20,30,40,50\nS,11,21,31,41,51\n";
    memcpy(dma,frames,strlen(frames));
    uart_dma_drain(&cursor,dma,256,7,&ring);
    uart_dma_drain(&cursor,dma,256,strlen(frames),&ring);
    for(unsigned n=0;n<strlen(frames);++n) { uint8_t b; assert(uart_ring_pop(&ring,&b)); assert(b==frames[n]); }
    memset(&cursor,0,sizeof cursor); memset(&ring,0,sizeof ring);
    for(unsigned n=0;n<256;++n) dma[n]=(uint8_t)n;
    uart_dma_drain(&cursor,dma,256,256,&ring);
    uart_dma_drain(&cursor,dma,256,128,&ring);
    uart_dma_drain(&cursor,dma,256,256,&ring);
    assert(ring.overflow==1 && ring.bytes_received==512); expect(0,511);
    memset(&cursor,0,sizeof cursor); memset(&ring,0,sizeof ring);
    unsigned position=0, seq=0;
    for(unsigned block=0;block<1000;++block) {
        unsigned length=1+block%127;
        for(unsigned n=0;n<length;++n) dma[(position+n)%256]=(uint8_t)(seq+n);
        position=(position+length)%256;
        assert(uart_dma_drain(&cursor,dma,256,position,&ring)==length);
        assert(uart_dma_drain(&cursor,dma,256,position,&ring)==0);
        expect(seq,length); seq+=length;
    }
    assert(!ring.overflow && cursor.bytes==seq);
    puts("PASS first/repeated/partial/end/wrap/frame-split/multiple-frames/ring-full/no-duplicate/no-loss");
    printf("supported-load sequential bytes=%u, windows=%u\n",seq,cursor.windows);
    return 0;
}
