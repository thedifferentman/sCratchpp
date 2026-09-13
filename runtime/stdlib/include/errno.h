#ifndef SCRATCH_ERRNO_H
#define SCRATCH_ERRNO_H
#ifdef __cplusplus
extern "C" {
#endif
int *__scratch_errno_location(void);
#ifdef __cplusplus
}
#endif
#define errno (*__scratch_errno_location())
#define EDOM 33
#define ERANGE 34
#define EINVAL 22
#define ENOMEM 12
#define ENOSYS 38
#define ENOENT 2
#endif
