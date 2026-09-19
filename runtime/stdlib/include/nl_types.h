#ifndef SCRATCH_NL_TYPES_H
#define SCRATCH_NL_TYPES_H
typedef void *nl_catd;
#define NL_CAT_LOCALE 1
#ifdef __cplusplus
extern "C" {
#endif
nl_catd catopen(const char*,int);
char *catgets(nl_catd,int,int,const char*);
int catclose(nl_catd);
#ifdef __cplusplus
}
#endif
#endif
