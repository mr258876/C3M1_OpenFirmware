#include <stddef.h>
#include <stdint.h>
/* Only for freestanding clang+lld build; regular CubeIDE uses newlib. */
void __libc_init_array(void) {}
void *memcpy(void *a,const void *b,size_t n) {
  unsigned char *d=a;const unsigned char *s=b;
  while(n--) *d++=*s++;
  return a;
}
void *memset(void *a,int c,size_t n) {
  unsigned char *d=a;while(n--) *d++=(unsigned char)c;
  return a;
}
void *memmove(void *a,const void *b,size_t n) {
  unsigned char *d=a;const unsigned char *s=b;
  if(d<s) {for(size_t i=0;i<n;i++)d[i]=s[i];}
  else {while(n--)d[n]=s[n];}
  return a;
}
int memcmp(const void *a,const void *b,size_t n) {
  const unsigned char *x=a,*y=b;
  for(size_t i=0;i<n;i++) if(x[i]!=y[i])return (int)x[i]-(int)y[i];
  return 0;
}
size_t strlen(const char *p) {size_t n=0;while(p[n])n++;return n;}
/* Clang emits ARM EABI memory helpers for unaligned 9-byte structures. */
void __aeabi_memcpy(void *dst,const void *src,size_t n) {
  (void)memcpy(dst,src,n);
}
void __aeabi_memcpy4(void *dst,const void *src,size_t n) {
  (void)memcpy(dst,src,n);
}
/* EABI emits these when zero-initializing fixed-size local bitmaps. */
void __aeabi_memclr(void *dst, size_t n) { (void)memset(dst, 0, n); }
void __aeabi_memclr4(void *dst, size_t n) { (void)memset(dst, 0, n); }
void __aeabi_memclr8(void *dst, size_t n) { (void)memset(dst, 0, n); }
