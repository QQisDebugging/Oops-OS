// readvwritevtest.c - readv/writev 分散聚集 I/O 测试

#include "kernel/include/types.h"
#include "kernel/include/stat.h"
#include "user/user.h"
#include "kernel/include/fcntl.h"

#define TEST_FILE "/readv_writev_test"

void test_basic_writev(void)
{
    printf("=== Test 1: 基本 writev 测试 ===\n");
    
    int fd = open(TEST_FILE, O_CREATE | O_RDWR);
    if (fd < 0) {
        printf("FAIL: 无法创建测试文件\n");
        exit(1);
    }
    
    // 准备多个缓冲区
    char buf1[] = "Hello";
    char buf2[] = " ";
    char buf3[] = "World";
    char buf4[] = "!";
    
    struct iovec iov[4];
    iov[0].iov_base = buf1;
    iov[0].iov_len = 5;
    iov[1].iov_base = buf2;
    iov[1].iov_len = 1;
    iov[2].iov_base = buf3;
    iov[2].iov_len = 5;
    iov[3].iov_base = buf4;
    iov[3].iov_len = 1;
    
    int ret = writev(fd, iov, 4);
    int expected = 12;
    
    if (ret == expected) {
        printf("PASS: writev 返回 %d 字节\n", ret);
    } else {
        printf("FAIL: writev 返回 %d, 期望 %d\n", ret, expected);
    }
    
    // 验证写入内容
    lseek(fd, 0, SEEK_SET);
    char verify[32];
    memset(verify, 0, sizeof(verify));
    read(fd, verify, 12);
    
    if (strcmp(verify, "Hello World!") == 0) {
        printf("PASS: 写入内容正确: '%s'\n", verify);
    } else {
        printf("FAIL: 写入内容错误: '%s'\n", verify);
    }
    
    close(fd);
    unlink(TEST_FILE);
    printf("\n");
}

void test_basic_readv(void)
{
    printf("=== Test 2: 基本 readv 测试 ===\n");
    
    int fd = open(TEST_FILE, O_CREATE | O_RDWR);
    if (fd < 0) {
        printf("FAIL: 无法创建测试文件\n");
        exit(1);
    }
    
    // 写入测试数据
    write(fd, "ABCDEFGHIJKLMNOP", 16);
    lseek(fd, 0, SEEK_SET);
    
    // 准备多个接收缓冲区
    char buf1[5], buf2[5], buf3[6];
    memset(buf1, 0, sizeof(buf1));
    memset(buf2, 0, sizeof(buf2));
    memset(buf3, 0, sizeof(buf3));
    
    struct iovec iov[3];
    iov[0].iov_base = buf1;
    iov[0].iov_len = 4;
    iov[1].iov_base = buf2;
    iov[1].iov_len = 4;
    iov[2].iov_base = buf3;
    iov[2].iov_len = 5;
    
    int ret = readv(fd, iov, 3);
    int expected = 13;
    
    if (ret == expected) {
        printf("PASS: readv 返回 %d 字节\n", ret);
    } else {
        printf("FAIL: readv 返回 %d, 期望 %d\n", ret, expected);
    }
    
    // 验证各缓冲区内容
    int success = 1;
    if (strcmp(buf1, "ABCD") != 0) {
        printf("FAIL: buf1 = '%s', 期望 'ABCD'\n", buf1);
        success = 0;
    }
    if (strcmp(buf2, "EFGH") != 0) {
        printf("FAIL: buf2 = '%s', 期望 'EFGH'\n", buf2);
        success = 0;
    }
    if (strcmp(buf3, "IJKLM") != 0) {
        printf("FAIL: buf3 = '%s', 期望 'IJKLM'\n", buf3);
        success = 0;
    }
    
    if (success) {
        printf("PASS: 所有缓冲区内容正确\n");
        printf("  buf1='%s', buf2='%s', buf3='%s'\n", buf1, buf2, buf3);
    }
    
    close(fd);
    unlink(TEST_FILE);
    printf("\n");
}

void test_partial_read(void)
{
    printf("=== Test 3: 部分读取测试 ===\n");
    
    int fd = open(TEST_FILE, O_CREATE | O_RDWR);
    write(fd, "SHORT", 5);
    lseek(fd, 0, SEEK_SET);
    
    char buf1[10], buf2[10];
    memset(buf1, 0, sizeof(buf1));
    memset(buf2, 0, sizeof(buf2));
    
    struct iovec iov[2];
    iov[0].iov_base = buf1;
    iov[0].iov_len = 8;  // 请求 8 字节，但文件只有 5 字节
    iov[1].iov_base = buf2;
    iov[1].iov_len = 8;
    
    int ret = readv(fd, iov, 2);
    
    if (ret == 5) {
        printf("PASS: readv 正确返回 5 (文件实际大小)\n");
    } else {
        printf("FAIL: readv 返回 %d, 期望 5\n", ret);
    }
    
    if (strcmp(buf1, "SHORT") == 0) {
        printf("PASS: buf1 内容正确: '%s'\n", buf1);
    } else {
        printf("FAIL: buf1 内容错误: '%s'\n", buf1);
    }
    
    close(fd);
    unlink(TEST_FILE);
    printf("\n");
}

void test_empty_iovec(void)
{
    printf("=== Test 4: 空 iovec 测试 ===\n");
    
    int fd = open(TEST_FILE, O_CREATE | O_RDWR);
    write(fd, "TESTDATA", 8);
    lseek(fd, 0, SEEK_SET);
    
    char buf1[5], buf2[5];
    memset(buf1, 0, sizeof(buf1));
    memset(buf2, 0, sizeof(buf2));
    
    struct iovec iov[3];
    iov[0].iov_base = buf1;
    iov[0].iov_len = 4;
    iov[1].iov_base = 0;
    iov[1].iov_len = 0;  // 空 iovec
    iov[2].iov_base = buf2;
    iov[2].iov_len = 4;
    
    int ret = readv(fd, iov, 3);
    
    if (ret == 8) {
        printf("PASS: readv 正确处理空 iovec, 返回 %d\n", ret);
        printf("  buf1='%s', buf2='%s'\n", buf1, buf2);
    } else {
        printf("FAIL: readv 返回 %d, 期望 8\n", ret);
    }
    
    close(fd);
    unlink(TEST_FILE);
    printf("\n");
}

int main(int argc, char *argv[])
{
    printf("========================================\n");
    printf("   readv/writev 分散聚集 I/O 测试\n");
    printf("========================================\n\n");
    
    test_basic_writev();
    test_basic_readv();
    test_partial_read();
    test_empty_iovec();
    
    printf("========================================\n");
    printf("   所有测试完成\n");
    printf("========================================\n");
    
    exit(0);
}
