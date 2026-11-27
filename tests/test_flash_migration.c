#define _GNU_SOURCE
/* Host simulation of current-format STM32F1 flash writes; legacy records rejected.
 * Maps 0x0807F000..0x0807FFFF to writable memory on Linux. */
#include "c3_settings.h"
#include "stm32f1xx_hal.h"
#include <sys/mman.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static int fail_after_program = -1;
static int program_count;
static uint32_t erase_addr;
HAL_StatusTypeDef HAL_FLASH_Unlock(void) { return HAL_OK; }
HAL_StatusTypeDef HAL_FLASH_Lock(void) { return HAL_OK; }
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *e,uint32_t *err)
{
    assert(e->TypeErase == FLASH_TYPEERASE_PAGES && e->NbPages == 1u);
    assert(e->PageAddress == C3_SETTINGS_PAGE_A || e->PageAddress == C3_SETTINGS_PAGE_B);
    erase_addr=e->PageAddress;
    memset((void *)(uintptr_t)e->PageAddress,0xFF,2048);
    *err=0xFFFFFFFFu;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type,uint32_t addr,uint64_t data)
{
    assert(type==FLASH_TYPEPROGRAM_HALFWORD && (addr&1u)==0u);
    assert(addr>=C3_SETTINGS_PAGE_A && addr+1u < C3_SETTINGS_PAGE_B+2048u);
    if (fail_after_program>=0 && program_count++>=fail_after_program) return HAL_ERROR;
    uint16_t *p=(uint16_t *)(uintptr_t)addr;
    uint16_t hw=(uint16_t)data;
    assert(((*p)&hw)==hw);  /* 1->0 only */
    *p=hw;
    return HAL_OK;
}
static uint32_t crc32(const void *mem,unsigned size)
{
    const uint8_t *s=(const uint8_t *)mem;
    uint32_t crc=0xFFFFFFFFu;
    for (unsigned i=0;i<size;i++) {
        crc^=s[i];
        for (unsigned j=0;j<8;j++) crc=(crc>>1u)^((crc&1u)?0xEDB88320u:0);
    }
    return ~crc;
}
static void blank(void)
{
    memset((void *)(uintptr_t)C3_SETTINGS_PAGE_A,0xFF,4096);
    fail_after_program=-1; program_count=0; erase_addr=0;
}
static void put(uint32_t page,const c3_settings_record_t *r)
{
    memcpy((void *)(uintptr_t)page,r,sizeof(*r));
}
int main(void)
{
    void *ptr=mmap((void *)(uintptr_t)C3_SETTINGS_PAGE_A,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
    assert(ptr!=(void *)-1);
    c3_settings_t def,a,b,s;
    c3_settings_defaults(&def); a=def; a.peaking=1; a.sleep_timer=4; b=a; b.sleep_timer=1;
#if C3_I18N
#if C3_LANGUAGE_SETUP
    c3_settings_set_ui_prefs(&a,0,1);
    c3_settings_set_ui_prefs(&b,1,1);
#else
    c3_settings_next(&b,C3_SETTING_LANGUAGE);
#endif
    assert(c3_settings_language(&b));
#if C3_STARTUP_HINT
    c3_settings_next(&b,C3_SETTING_STARTUP_HINT);
    assert(c3_settings_language(&b)&&!c3_settings_startup_hint(&b));
#endif
#endif
    c3_settings_record_t rec,other;
    blank(); memset(&s,0xCC,sizeof(s));
    assert(!c3_settings_flash_load(&s) && !memcmp(&s,&def,sizeof(s)));
    for(unsigned version=1;version<6;version++) {
        blank(); c3_settings_make_record(&rec,&a,20); rec.version=version; rec.crc=crc32(&rec,24); put(C3_SETTINGS_PAGE_B,&rec);
        assert(!c3_settings_flash_load(&s) && !memcmp(&s,&def,sizeof(s)));
        assert(!memcmp((void *)(uintptr_t)C3_SETTINGS_PAGE_B,&rec,sizeof(rec)));
        assert(c3_settings_flash_save(&b) && erase_addr==C3_SETTINGS_PAGE_A);
        assert(c3_settings_flash_load(&s) && !memcmp(&s,&b,sizeof(s)));
    }
    /* Both directions, every torn-write point, sequence wraps. */
    unsigned n=0;
    for(unsigned page=0;page<2;page++) for(int cut=0;cut<=15;cut++) {
        blank(); c3_settings_make_record(&rec,&a,0xFFFFFFFFu);
        c3_settings_make_record(&other,&def,0xFFFFFFFEu);
        uint32_t current=page?C3_SETTINGS_PAGE_B:C3_SETTINGS_PAGE_A;
        uint32_t target=page?C3_SETTINGS_PAGE_A:C3_SETTINGS_PAGE_B;
        put(current,&rec); put(target,&other);
        assert(c3_settings_flash_load(&s) && !memcmp(&s,&a,sizeof(s)));
        assert(c3_settings_flash_save(&a) && erase_addr==0); /* no-op */
        fail_after_program=cut; program_count=0;
        assert(c3_settings_flash_save(&b)==(cut==15)); assert(erase_addr==target);
        assert(!memcmp((void *)(uintptr_t)current,&rec,sizeof(rec)));
        assert(c3_settings_flash_load(&s) && !memcmp(&s,cut==15?&b:&a,sizeof(s)));
        fail_after_program=-1; assert(c3_settings_flash_save(&b));
        assert(c3_settings_flash_load(&s) && !memcmp(&s,&b,sizeof(s)));
        ++n;
    }
    blank(); c3_settings_make_record(&rec,&a,99); rec.crc^=1; put(C3_SETTINGS_PAGE_A,&rec);
    assert(!c3_settings_flash_load(&s) && !memcmp(&s,&def,sizeof(s)));
    blank(); c3_settings_make_record(&rec,&a,100); rec.commit=0xFFFF; put(C3_SETTINGS_PAGE_A,&rec);
    assert(!c3_settings_flash_load(&s));
#if C3_MENU_ACTIONS
    /* Factory settings reset writes defaults as a new v6 record, not a
     * destructive erase of both records. Every torn write preserves old settings. */
    for(unsigned page=0;page<2;page++)for(int cut=0;cut<=15;cut++){
        blank();c3_settings_make_record(&rec,&b,7);put(page?C3_SETTINGS_PAGE_B:C3_SETTINGS_PAGE_A,&rec);
        fail_after_program=cut;program_count=0;
        assert(c3_settings_flash_save(&def)==(cut==15));
        assert(c3_settings_flash_load(&s)&&!memcmp(&s,cut==15?&def:&b,sizeof(s)));
    }
    puts("PASS factory reset defaults: 32 torn-write cases preserve previous settings or commit defaults");
#endif
#if C3_LANGUAGE_SETUP
    /* Every cut before commit must preserve pending / previously chosen
       language. Only a successful write may allow exit from mandatory setup. */
    unsigned choices=0;
    for(unsigned state=0;state<(C3_POSTSHOT?33u:9u);state++)for(unsigned lang=0;lang<2;lang++)for(int cut=0;cut<=15;cut++){
        blank();c3_settings_t prior=def;
        if(state<(C3_POSTSHOT?32u:8u)){
            prior.peak_area=state;prior.grid=1;prior.sleep_timer=2;
            assert(c3_settings_flash_save(&prior));
        }
        c3_settings_t chosen;
        assert(c3_settings_flash_load(&chosen)==(state<(C3_POSTSHOT?32u:8u)));
        assert(!memcmp(&prior,&chosen,sizeof prior));
        fail_after_program=cut;program_count=0;
        c3_settings_t expected=prior;c3_settings_set_ui_prefs(&expected,lang,c3_settings_startup_hint(&prior));
        int noop=(state<(C3_POSTSHOT?32u:8u)&&!memcmp(&prior,&expected,sizeof prior));
        int success=(cut==15||noop);
        assert(c3_settings_flash_confirm_language(&chosen,lang)==success);
        assert(!memcmp(&chosen,success?&expected:&prior,sizeof chosen));
        assert(c3_settings_flash_load(&s)==(success||state<(C3_POSTSHOT?32u:8u)));
        assert(!memcmp(&s,&chosen,sizeof s));
        if(!success){
            fail_after_program=-1;assert(c3_settings_flash_confirm_language(&chosen,lang));
            assert(!c3_settings_language_pending(&chosen));
            assert(c3_settings_flash_load(&s)&&!memcmp(&s,&expected,sizeof s));
        }
        choices++;
    }
    blank();assert(c3_settings_flash_save(&def));
    assert(c3_settings_flash_load(&s)&&c3_settings_language_pending(&s));
    assert(c3_settings_startup_hint(&s));
    assert(!c3_settings_flash_confirm_language(&s,2)&&c3_settings_language_pending(&s));
    printf("PASS language confirmation: %u Flash cut/state/language cases, pending persists until committed, retry safe\n",choices);
#endif
    munmap(ptr,4096);
    printf("PASS current v6 Flash: %u torn-write cases, legacy v1-v5 rejection, CRC, commit, sequence wrap, no-op\n",n);
}
