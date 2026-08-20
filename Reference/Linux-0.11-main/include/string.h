#ifndef _STRING_H_
#define _STRING_H_

#ifndef NULL
#define NULL ((void *) 0)
#endif

#ifndef _SIZE_T
#define _SIZE_T
typedef unsigned int size_t;
#endif

extern char * strerror(int errno);

/* SYMPLUS-PORT: 原 19 个 extern inline 汇编字符串函数（gcc 内联汇编 +
 * register __asm__("ax") 绑定）→ static 纯 C 实现（每编译单元私有拷贝，
 * 避免多文件重复定义）。语义等价，M3 运行期验证。 */

static char *strcpy(char *dest, const char *src)
{
	char *d = dest;
	while ((*d++ = *src++) != 0)
		;
	return dest;
}

static char *strncpy(char *dest, const char *src, int count)
{
	char *d = dest;
	while (count-- && (*d++ = *src++) != 0)
		;
	while (count-- > 0)
		*d++ = 0;
	return dest;
}

static char *strcat(char *dest, const char *src)
{
	char *d = dest;
	while (*d)
		d++;
	while ((*d++ = *src++) != 0)
		;
	return dest;
}

static char *strncat(char *dest, const char *src, int count)
{
	char *d = dest;
	while (*d)
		d++;
	while (count-- && (*d++ = *src++) != 0)
		;
	*d = 0;
	return dest;
}

static int strcmp(const char *cs, const char *ct)
{
	while (*cs && *cs == *ct) {
		cs++;
		ct++;
	}
	return (unsigned char)*cs - (unsigned char)*ct;
}

static int strncmp(const char *cs, const char *ct, int count)
{
	while (count-- && *cs && *cs == *ct) {
		cs++;
		ct++;
	}
	return count < 0 ? 0 : (unsigned char)*cs - (unsigned char)*ct;
}

static char *strchr(const char *s, char c)
{
	while (*s) {
		if (*s == c)
			return (char *)s;
		s++;
	}
	return c == 0 ? (char *)s : NULL;
}

static char *strrchr(const char *s, char c)
{
	const char *last = NULL;
	while (*s) {
		if (*s == c)
			last = s;
		s++;
	}
	return c == 0 ? (char *)s : (char *)last;
}

static int strspn(const char *cs, const char *ct)
{
	const char *p = cs;
	while (*p && strchr(ct, *p))
		p++;
	return p - cs;
}

static int strcspn(const char *cs, const char *ct)
{
	const char *p = cs;
	while (*p && !strchr(ct, *p))
		p++;
	return p - cs;
}

static char *strpbrk(const char *cs, const char *ct)
{
	const char *p = cs;
	while (*p && !strchr(ct, *p))
		p++;
	return *p ? (char *)p : NULL;
}

static char *strstr(const char *cs, const char *ct)
{
	const char *p;
	int n;
	if (!*ct)
		return (char *)cs;
	n = strlen(ct);
	for (p = cs; *p; p++)
		if (*p == *ct && strncmp(p, ct, n) == 0)
			return (char *)p;
	return NULL;
}

static int strlen(const char *s)
{
	const char *p = s;
	while (*p)
		p++;
	return p - s;
}

extern char *___strtok;

/* 标准 strtok 语义（原汇编版同样维护 ___strtok 跨调用状态） */
static char *strtok(char *s, const char *ct)
{
	char *start;
	if (!s)
		s = ___strtok;
	if (!s)
		return NULL;
	while (*s && strchr(ct, *s))
		s++;
	if (!*s) {
		___strtok = NULL;
		return NULL;
	}
	start = s;
	while (*s && !strchr(ct, *s))
		s++;
	if (*s) {
		*s = 0;
		___strtok = s + 1;
	} else {
		___strtok = NULL;
	}
	return start;
}

static void *memcpy(void *dest, const void *src, int n)
{
	char *d = (char *)dest;
	const char *s = (const char *)src;
	while (n--)
		*d++ = *s++;
	return dest;
}

static void *memmove(void *dest, const void *src, int n)
{
	char *d = (char *)dest;
	const char *s = (const char *)src;
	if (d < s) {
		while (n--)
			*d++ = *s++;
	} else {
		d += n;
		s += n;
		while (n--)
			*--d = *--s;
	}
	return dest;
}

static int memcmp(const void *cs, const void *ct, int count)
{
	const char *a = (const char *)cs;
	const char *b = (const char *)ct;
	while (count--) {
		if (*a != *b)
			return *a < *b ? -1 : 1;
		a++;
		b++;
	}
	return 0;
}

static void *memchr(const void *cs, char c, int count)
{
	const char *p = (const char *)cs;
	while (count--) {
		if (*p == c)
			return (void *)p;
		p++;
	}
	return NULL;
}

static void *memset(void *s, char c, int count)
{
	char *p = (char *)s;
	while (count--)
		*p++ = c;
	return s;
}

#endif
