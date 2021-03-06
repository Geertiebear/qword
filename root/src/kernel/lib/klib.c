#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <lib/lock.h>
#include <lib/klib.h>
#include <lib/qemu.h>
#include <misc/tty.h>
#include <mm/mm.h>
#include <lib/time.h>
#include <fd/vfs/vfs.h>

int ktolower(int c) {
    if (c >= 0x41 && c <= 0x5a)
        return c + 0x20;
    return c;
}

char *kstrchrnul(const char *s, int c) {
    while (*s)
        if ((*s++) == c)
            break;
    return (char *)s;
}

char *kstrcpy(char *dest, const char *src) {
    size_t i = 0;

    for (i = 0; src[i]; i++)
        dest[i] = src[i];

    dest[i] = 0;

    return dest;
}

int kstrcmp(const char *dst, const char *src) {
    size_t i;

    for (i = 0; dst[i] == src[i]; i++) {
        if ((!dst[i]) && (!src[i])) return 0;
    }

    return 1;
}

int kstrncmp(const char *dst, const char *src, size_t count) {
    size_t i;

    for (i = 0; i < count; i++)
        if (dst[i] != src[i]) return 1;

    return 0;
}

size_t kstrlen(const char *str) {
    size_t len;

    for (len = 0; str[len]; len++);

    return len;
}

void readline(int fd, const char *prompt, char *str, size_t max) {
    size_t i;
    write(fd, prompt, kstrlen(prompt));
    for (i = 0; i < (max - 1); i++) {
        read(fd, &str[i], 1);
        if (str[i] == '\n')
            break;
    }
    str[i] = 0;
    return;
}

#define KPRINT_BUF_MAX 256

static void kputs(char *kprint_buf, size_t *kprint_buf_i, const char *string) {
    size_t i;

    for (i = 0; string[i]; i++) {
        if (*kprint_buf_i == (KPRINT_BUF_MAX - 1))
            break;
        kprint_buf[(*kprint_buf_i)++] = string[i];
    }

    kprint_buf[*kprint_buf_i] = 0;

    return;
}

static void knputs(char *kprint_buf, size_t *kprint_buf_i, const char *string, size_t len) {
    size_t i;

    for (i = 0; i < len; i++) {
        if (*kprint_buf_i == (KPRINT_BUF_MAX - 1))
            break;
        kprint_buf[(*kprint_buf_i)++] = string[i];
    }

    kprint_buf[*kprint_buf_i] = 0;

    return;
}

static void kputchar(char *kprint_buf, size_t *kprint_buf_i, char c) {
    if (*kprint_buf_i < (KPRINT_BUF_MAX - 1)) {
        kprint_buf[(*kprint_buf_i)++] = c;
    }

    kprint_buf[*kprint_buf_i] = 0;

    return;
}

static void kprint_buf_flush(char *kprint_buf, size_t *kprint_buf_i) {
    #ifdef _KERNEL_QEMU_
        qemu_debug_puts(kprint_buf);
    #endif
    #ifdef _KERNEL_VGA_
        tty_write(0, kprint_buf, 0, *kprint_buf_i);
    #endif
    return;
}

static void kprint_buf_flush_panic(char *kprint_buf, size_t *kprint_buf_i) {
    qemu_debug_puts(kprint_buf);
    return;
}

static void kprn_i(char *kprint_buf, size_t *kprint_buf_i, int64_t x) {
    int i;
    char buf[21] = {0};

    if (!x) {
        kputchar(kprint_buf, kprint_buf_i, '0');
        return;
    }

    int sign = x < 0;
    if (sign) x = -x;

    for (i = 19; x; i--) {
        buf[i] = (x % 10) + 0x30;
        x = x / 10;
    }
    if (sign)
        buf[i] = '-';
    else
        i++;

    kputs(kprint_buf, kprint_buf_i, buf + i);

    return;
}

static void kprn_ui(char *kprint_buf, size_t *kprint_buf_i, uint64_t x) {
    int i;
    char buf[21] = {0};

    if (!x) {
        kputchar(kprint_buf, kprint_buf_i, '0');
        return;
    }

    for (i = 19; x; i--) {
        buf[i] = (x % 10) + 0x30;
        x = x / 10;
    }

    i++;
    kputs(kprint_buf, kprint_buf_i, buf + i);

    return;
}

static const char hex_to_ascii_tab[] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
};

static void kprn_x(char *kprint_buf, size_t *kprint_buf_i, uint64_t x) {
    int i;
    char buf[17] = {0};

    if (!x) {
        kputs(kprint_buf, kprint_buf_i, "0x0");
        return;
    }

    for (i = 15; x; i--) {
        buf[i] = hex_to_ascii_tab[(x % 16)];
        x = x / 16;
    }

    i++;
    kputs(kprint_buf, kprint_buf_i, "0x");
    kputs(kprint_buf, kprint_buf_i, buf + i);

    return;
}

static void print_timestamp(char *kprint_buf, size_t *kprint_buf_i, int type) {
    kputs(kprint_buf, kprint_buf_i, "\e[37m[");
    kprn_ui(kprint_buf, kprint_buf_i, uptime_sec);
    kputs(kprint_buf, kprint_buf_i, ".");
    kprn_ui(kprint_buf, kprint_buf_i, uptime_raw);
    kputs(kprint_buf, kprint_buf_i, "] ");

    switch (type) {
        case KPRN_INFO:
            kputs(kprint_buf, kprint_buf_i, "\e[36minfo\e[37m: ");
            break;
        case KPRN_WARN:
            kputs(kprint_buf, kprint_buf_i, "\e[33mwarning\e[37m: ");
            break;
        case KPRN_ERR:
            kputs(kprint_buf, kprint_buf_i, "\e[31mERROR\e[37m: ");
            break;
        case KPRN_PANIC:
            kputs(kprint_buf, kprint_buf_i, "\e[31mPANIC\e[37m: ");
            break;
        default:
        case KPRN_DBG:
            kputs(kprint_buf, kprint_buf_i, "\e[36mDEBUG\e[37m: ");
            break;
    }
}

void kprint(int type, const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);

    char kprint_buf[KPRINT_BUF_MAX];
    size_t kprint_buf_i = 0;

    print_timestamp(kprint_buf, &kprint_buf_i, type);

    char *str;
    size_t str_len;

    for (;;) {
        char c;

        while (*fmt && *fmt != '%') {
            kputchar(kprint_buf, &kprint_buf_i, *fmt);
            if (*fmt == '\n')
                print_timestamp(kprint_buf, &kprint_buf_i, type);
            fmt++;
        }
        if (!*fmt++) {
            va_end(args);
            kputchar(kprint_buf, &kprint_buf_i, '\n');
            goto out;
        }
        switch (*fmt++) {
            case 's':
                str = (char *)va_arg(args, const char *);
                if (!str)
                    kputs(kprint_buf, &kprint_buf_i, "(null)");
                else
                    kputs(kprint_buf, &kprint_buf_i, str);
                break;
            case 'S':
                str_len = va_arg(args, size_t);
                str = (char *)va_arg(args, const char *);
                knputs(kprint_buf, &kprint_buf_i, str, str_len);
                break;
            case 'd':
                kprn_i(kprint_buf, &kprint_buf_i, (int64_t)va_arg(args, int));
                break;
            case 'D':
                kprn_i(kprint_buf, &kprint_buf_i, (int64_t)va_arg(args, int64_t));
                break;
            case 'u':
                kprn_ui(kprint_buf, &kprint_buf_i, (uint64_t)va_arg(args, unsigned int));
                break;
            case 'U':
                kprn_ui(kprint_buf, &kprint_buf_i, (uint64_t)va_arg(args, uint64_t));
                break;
            case 'x':
                kprn_x(kprint_buf, &kprint_buf_i, (uint64_t)va_arg(args, unsigned int));
                break;
            case 'X':
                kprn_x(kprint_buf, &kprint_buf_i, (uint64_t)va_arg(args, uint64_t));
                break;
            case 'c':
                c = (char)va_arg(args, int);
                kputchar(kprint_buf, &kprint_buf_i, c);
                break;
            default:
                kputchar(kprint_buf, &kprint_buf_i, '?');
                break;
        }
    }

out:
    if (type != KPRN_PANIC) {
        kprint_buf_flush(kprint_buf, &kprint_buf_i);
    } else {
        kprint_buf_flush_panic(kprint_buf, &kprint_buf_i);
    }

    return;
}

typedef struct {
    size_t pages;
    size_t size;
} alloc_metadata_t;

void *kalloc(size_t size) {
    size_t page_count = size / PAGE_SIZE;

    if (size % PAGE_SIZE) page_count++;

    char *ptr = pmm_allocz(page_count + 1);

    if (!ptr) {
        return (void *)0;
    }

    ptr += MEM_PHYS_OFFSET;

    alloc_metadata_t *metadata = (alloc_metadata_t *)ptr;
    ptr += PAGE_SIZE;

    metadata->pages = page_count;
    metadata->size = size;

    return (void *)ptr;
}

void kfree(void *ptr) {
    alloc_metadata_t *metadata = (alloc_metadata_t *)((size_t)ptr - PAGE_SIZE);

    pmm_free((void *)((size_t)metadata - MEM_PHYS_OFFSET), metadata->pages + 1);
}

void *krealloc(void *ptr, size_t new) {
    /* check if 0 */
    if (!ptr) return kalloc(new);
    if (!new) {
        kfree(ptr);
        return (void *)0;
    }

    /* Reference metadata page */
    alloc_metadata_t *metadata = (alloc_metadata_t *)((size_t)ptr - PAGE_SIZE);

    if ((metadata->size + PAGE_SIZE - 1) / PAGE_SIZE
         == (new + PAGE_SIZE - 1) / PAGE_SIZE) {
        metadata->size = new;
        return ptr;
    }

    char *new_ptr;
    if ((new_ptr = kalloc(new)) == 0) {
        return (void *)0;
    }

    if (metadata->size > new)
        /* Copy all the data from the old pointer to the new pointer,
         * within the range specified by `size`. */
        kmemcpy(new_ptr, (char *)ptr, new);
    else
        kmemcpy(new_ptr, (char *)ptr, metadata->size);

    kfree(ptr);

    return new_ptr;
}

void *kmemcpy(void *dest, const void *src, size_t count) {
    size_t i = 0;

    uint8_t *dest2 = dest;
    const uint8_t *src2 = src;

    for (i = 0; i < count; i++) {
        dest2[i] = src2[i];
    }

    return dest;
}

void *kmemcpy64(void *dest, const void *src, size_t count) {
    size_t i = 0;

    uint64_t *dest2 = dest;
    const uint64_t *src2 = src;

    size_t new_count = count / sizeof(uint64_t);
    for (i = 0; i < new_count; i++) {
        dest2[i] = src2[i];
    }

    return dest;
}

void *kmemset(void *s, int c, size_t count) {
    uint8_t *p = s, *end = p + count;
    for (; p != end; p++) {
        *p = (uint8_t)c;
    }

    return s;
}

void *kmemset64(void *ptr, uint64_t c, size_t count) {
    uint64_t *p = ptr;

    for (size_t i = 0; i < count; i++) {
        p[i] = c;
    }

    return ptr;
}

void *kmemmove(void *dest, const void *src, size_t count) {
    size_t i = 0;

    uint8_t *dest2 = dest;
    const uint8_t *src2 = src;

    if (src > dest) {
        for (i = 0; i < count; i++) {
            dest2[i] = src2[i];
        }
    } else if (src < dest) {
        for (i = count; i > 0; i--) {
            dest2[i - 1] = src2[i - 1];
        }
    }

    return dest;
}

int kmemcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *a = s1;
    const uint8_t *b = s2;

    for (size_t i = 0; i < n; i++) {
        if (a[i] < b[i]) {
            return -1;
        } else if (a[i] > b[i]) {
            return 1;
        }
    }

    return 0;
}

int ht_init(struct hashtable_t *table, int size) {
    table->size = size;
    table->num_entries = 0;
    table->buckets = kalloc(sizeof(struct ht_entry_t*) * table->size);
    if (!table->buckets)
        return -1;
    for (int i = 0; i < table->size; i++)
        table->buckets[i] = NULL;
    return 0;
}

int ht_add(struct hashtable_t *table, struct ht_entry_t *new_entry,
        uint64_t hash) {
    /* if load factor is greater than or equal to 0.75, reallocate */
    if (4*(table->num_entries + 1) >= 3*(table->size)) {
        struct hashtable_t temp_table;
        ht_init(&temp_table, table->size * 2);

        for (int i = 0; i < table->size; i++) {
            struct ht_entry_t *bucket = table->buckets[i];
            for (; bucket; bucket = bucket->next)
                ht_add(&temp_table, bucket, bucket->hash);
        }

        table->size = temp_table.size;
        table->num_entries = temp_table.num_entries;
        kfree(table->buckets);
        table->buckets = temp_table.buckets;
    }
    int pos = (hash & (table->size - 1));

    if (table->buckets[pos]) {
        struct ht_entry_t *entry = NULL;
        for (entry = table->buckets[pos]; entry->next; entry = entry->next);
        entry->next = new_entry;
        entry->next->next = NULL;
        entry->next->hash = hash;
        table->num_entries++;
        return 0;
    }

    table->buckets[pos] = new_entry;
    table->buckets[pos]->next= NULL;
    table->buckets[pos]->hash = hash;
    table->num_entries++;
    return 0;
}

struct ht_entry_t *ht_get_bucket(struct hashtable_t *table, uint64_t hash) {
    int pos = (hash & (table->size - 1));
    if (pos >= table->size)
        return NULL;
    return table->buckets[pos];
}

struct ht_entry_t *ht_remove_entry(struct hashtable_t *table,
        struct ht_entry_t *entry, struct ht_entry_t *prev) {
    if (!prev) {
        int pos = (entry->hash & (table->size - 1));
        table->buckets[pos] = entry->next;
        table->num_entries--;
        return entry;
    }

    prev->next = entry->next;
    table->num_entries--;
    return entry;
}

uint64_t ht_hash_str(const char *str) {
    /* djb2
     * http://www.cse.yorku.ca/~oz/hash.html
     */
    uint64_t hash = 5381;
    int c;
    while (c = *str++)
        hash = ((hash << 5) + hash) + c;
    return hash;
}
