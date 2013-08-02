#include <sys/types.h>

//#define __USE_GNU

#include <dlfcn.h>

/* dl*() stub routines for static compilation.  Prepared from
   /usr/include/dlfcn.h by Hal Pomeranz <hal@deer-run.com> */

void *dlopen(const char *str, int x) {return (void *)0;}
void *dlsym(void *ptr, const char *str) {return (void *)0;}
int dlclose(void *ptr) {return 0;}
char *dlerror() {return (char *)0;}
void *dlmopen(Lmid_t a, const char *str, int x) {return (void *)0;}
int dladdr(const void *ptr1, Dl_info *ptr2) {return 0;}
int dldump(const char *str1, const char *str2, int x) {return 0;}
int dlinfo(void *ptr1, int x, void *ptr2) {return 0;}

void *_dlopen(const char *str, int x) {return (void *)0;}
void *_dlsym(void *ptr, const char *str) {return (void *)0;}
int _dlclose(void *ptr) {return 0;}
char *_dlerror() {return (char *)0;}
void *_dlmopen(Lmid_t a, const char *str, int x) {return (void *)0;}
int _dladdr(void *ptr1, Dl_info *ptr2) {return 0;}
int __dladdr(void *ptr1, Dl_info *ptr2) {return 0;}
int _dldump(const char *str1, const char *str2, int x) {return 0;}
int _dlinfo(void *ptr1, int x, void *ptr2) {return 0;}
