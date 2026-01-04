// dup2test.c - dup2 系统调用测试
// 测试复制文件描述符到指定编号

#include "kernel/include/types.h"
#include "kernel/include/stat.h"
#include "user/user.h"
#include "kernel/include/fcntl.h"

#define TEST_FILE "/dup2_test"

void test_basic_dup2(void)
{
    printf("=== Test 1: 基本 dup2 测试 ===\n");
    
    int fd = open(TEST_FILE, O_CREATE | O_RDWR);
    if (fd < 0) {
        printf("FAIL: 无法创建测试文件\n");
        exit(1);
    }
    
    // 写入测试数据
    write(fd, "HELLO", 5);
    lseek(fd, 0, SEEK_SET);
    
    // dup2 到新的文件描述符
    int newfd = 10;
    int ret = dup2(fd, newfd);
    
    if (ret == newfd) {
        printf("PASS: dup2 返回正确的新 fd: %d\n", ret);
    } else {
        printf("FAIL: dup2 返回 %d, 期望 %d\n", ret, newfd);
    }
    
    // 通过新 fd 读取数据
    char buf[16];
    memset(buf, 0, sizeof(buf));
    int n = read(newfd, buf, 5);
    
    if (n == 5 && strcmp(buf, "HELLO") == 0) {
        printf("PASS: 通过新 fd 读取数据成功: '%s'\n", buf);
    } else {
        printf("FAIL: 通过新 fd 读取失败\n");
    }
    
    close(fd);
    close(newfd);
    unlink(TEST_FILE);
    printf("\n");
}

void test_dup2_same_fd(void)
{
    printf("=== Test 2: dup2 相同 fd 测试 ===\n");
    
    int fd = open(TEST_FILE, O_CREATE | O_RDWR);
    if (fd < 0) {
        printf("FAIL: 无法创建测试文件\n");
        exit(1);
    }
    
    // dup2(fd, fd) 应该直接返回 fd
    int ret = dup2(fd, fd);
    
    if (ret == fd) {
        printf("PASS: dup2(fd, fd) 返回 fd: %d\n", ret);
    } else {
        printf("FAIL: dup2(fd, fd) 返回 %d, 期望 %d\n", ret, fd);
    }
    
    close(fd);
    unlink(TEST_FILE);
    printf("\n");
}

void test_dup2_close_existing(void)
{
    printf("=== Test 3: dup2 关闭已存在 fd 测试 ===\n");
    
    int fd1 = open(TEST_FILE, O_CREATE | O_RDWR);
    write(fd1, "FILE1", 5);
    
    char *file2 = "/dup2_test2";
    int fd2 = open(file2, O_CREATE | O_RDWR);
    write(fd2, "FILE2", 5);
    
    // dup2(fd1, fd2) 应该关闭 fd2 原来的文件
    int ret = dup2(fd1, fd2);
    
    if (ret == fd2) {
        printf("PASS: dup2 返回正确\n");
    } else {
        printf("FAIL: dup2 返回错误\n");
    }
    
    // 现在 fd2 应该指向 fd1 的文件
    lseek(fd2, 0, SEEK_SET);
    char buf[16];
    memset(buf, 0, sizeof(buf));
    read(fd2, buf, 5);
    
    if (strcmp(buf, "FILE1") == 0) {
        printf("PASS: fd2 现在指向 fd1 的文件内容: '%s'\n", buf);
    } else {
        printf("FAIL: fd2 内容错误: '%s'\n", buf);
    }
    
    close(fd1);
    close(fd2);
    unlink(TEST_FILE);
    unlink(file2);
    printf("\n");
}

void test_dup2_invalid(void)
{
    printf("=== Test 4: dup2 无效参数测试 ===\n");
    
    int ret;
    
    // 无效的 oldfd
    ret = dup2(100, 5);
    if (ret == -1) {
        printf("PASS: 无效 oldfd 返回 -1\n");
    } else {
        printf("FAIL: 无效 oldfd 应返回 -1, 实际 %d\n", ret);
    }
    
    // 无效的 newfd (负数)
    int fd = open(TEST_FILE, O_CREATE | O_RDWR);
    ret = dup2(fd, -1);
    if (ret == -1) {
        printf("PASS: 负数 newfd 返回 -1\n");
    } else {
        printf("FAIL: 负数 newfd 应返回 -1, 实际 %d\n", ret);
    }
    
    close(fd);
    unlink(TEST_FILE);
    printf("\n");
}

int main(int argc, char *argv[])
{
    printf("========================================\n");
    printf("   dup2 系统调用测试\n");
    printf("========================================\n\n");
    
    test_basic_dup2();
    test_dup2_same_fd();
    test_dup2_close_existing();
    test_dup2_invalid();
    
    printf("========================================\n");
    printf("   所有测试完成\n");
    printf("========================================\n");
    
    exit(0);
}
