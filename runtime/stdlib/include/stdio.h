#ifndef SCRATCH_STDIO_H
#define SCRATCH_STDIO_H
#include <stddef.h>
#include <stdarg.h>
#define EOF (-1)
#define BUFSIZ 1024
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#ifdef __cplusplus
extern "C" {
#endif
typedef struct __scratch_FILE FILE;
typedef long fpos_t;
extern FILE *stdin,*stdout,*stderr;
int fclose(FILE*); int fflush(FILE*); void setbuf(FILE*,char*); int setvbuf(FILE*,char*,int,size_t);
int fgetc(FILE*); int getc(FILE*); int getchar(void); char *fgets(char*,int,FILE*);
int ungetc(int,FILE*); int fputc(int,FILE*); int putc(int,FILE*); int putchar(int);
int fputs(const char*,FILE*); int puts(const char*);
size_t fread(void*,size_t,size_t,FILE*); size_t fwrite(const void*,size_t,size_t,FILE*);
void clearerr(FILE*); int feof(FILE*); int ferror(FILE*);
int fseek(FILE*,long,int); long ftell(FILE*); int fgetpos(FILE*,fpos_t*); int fsetpos(FILE*,const fpos_t*); void rewind(FILE*);
int snprintf(char*,size_t,const char*,...); int vsnprintf(char*,size_t,const char*,va_list);
int sprintf(char*,const char*,...); int vsprintf(char*,const char*,va_list);
int asprintf(char**,const char*,...); int vasprintf(char**,const char*,va_list);
int fprintf(FILE*,const char*,...); int vfprintf(FILE*,const char*,va_list);
int printf(const char*,...); int vprintf(const char*,va_list);
int sscanf(const char*,const char*,...);
int remove(const char*); int rename(const char*,const char*);
FILE *fopen(const char*,const char*); FILE *freopen(const char*,const char*,FILE*);
#ifdef __cplusplus
}
#endif
#endif
