// preadwritetest.c - pread/pwrite 位置读写系统调用测试
// 测试在指定偏移位置读写，验证不会修改文件偏移量

#include "kernel/include/types.h"
#include "kernel/include/stat.h"
#include "user/user.h"
#include "kernel/include/fcntl.h"

#define TEST_FILE "/preadwrite_test"
#define BUF_SIZE 128

void test_basic_pwrite(void)
{
    printf("=== Test 1: 基本 pwrite 测试 ===\n");
    
    int fd = open(TEST_FILE, O_CREATE | O_RDWR);
    if (fd < 0) {
        printf("FAIL: 无法创建测试文件\n");
        exit(1);
    }
    
    // 先写入一些初始数据
    char *init_data = "AAAAAAAAAAAAAAAA"; // 16 字节
    if (write(fd, init_data, 16) != 16) {
        printf("FAIL: 初始写入失败\n");
        close(fd);
        exit(1);
    }
    
    // 记录当前文件偏移
    int offset_before = lseek(fd, 0, SEEK_CUR);
    printf("pwrite 前偏移量: %d\n", offset_before);
    
    // 使用 pwrite 在偏移 4 处写入数据
    char *pwrite_data = "BBBB";
    int ret = pwrite(fd, pwrite_data, 4, 4);
    if (ret != 4) {
        printf("FAIL: pwrite 返回 %d, 期望 4\n", ret);
        close(fd);
        exit(1);
    }
    
    // 检查文件偏移是否保持不变
    int offset_after = lseek(fd, 0, SEEK_CUR);
    printf("pwrite 后偏移量: %d\n", offset_after);
    
    if (offset_before == offset_after) {
        printf("PASS: pwrite 不改变文件偏移量\n");
    } else {
        printf("FAIL: pwrite 改变了文件偏移量 (%d -> %d)\n", 
               offset_before, offset_after);
    }
    
    // 验证写入内容
    lseek(fd, 0, SEEK_SET);
    char buf[32];
    memset(buf, 0, sizeof(buf));
    read(fd, buf, 16);
    
    printf("文件内容: '%s'\n", buf);
    if (buf[0] == 'A' && buf[4] == 'B' && buf[8] == 'A') {
        printf("PASS: pwrite 写入位置正确\n");
    } else {
        printf("FAIL: pwrite 写入位置错误\n");
    }
    
    close(fd);
    unlink(TEST_FILE);
    printf("\n");
}

void test_basic_pread(void)
{
    printf("=== Test 2: 基本 pread 测试 ===\n");
    
    int fd = open(TEST_FILE, O_CREATE | O_RDWR);
    if (fd < 0) {
        printf("FAIL: 无法创建测试文件\n");
        exit(1);
    }
    
    // 写入测试数据
    char *data = "0123456789ABCDEF"; // 16 字节
    if (write(fd, data, 16) != 16) {
        printf("FAIL: 写入测试数据失败\n");
        close(fd);
        exit(1);
    }
    
    // 将文件偏移设置到中间位置
    lseek(fd, 8, SEEK_SET);
    int offset_before = lseek(fd, 0, SEEK_CUR);
    printf("pread 前偏移量: %d\n", offset_before);
    
    // 使用 pread 从偏移 0 读取
    char buf[32];
    memset(buf, 0, sizeof(buf));
    int ret = pread(fd, buf, 4, 0);
    if (ret != 4) {
        printf("FAIL: pread 返回 %d, 期望 4\n", ret);
        close(fd);
        exit(1);
    }
    
    // 检查文件偏移是否保持不变
    int offset_after = lseek(fd, 0, SEEK_CUR);
    printf("pread 后偏移量: %d\n", offset_after);
    
    if (offset_before == offset_after) {
        printf("PASS: pread 不改变文件偏移量\n");
    } else {
        printf("FAIL: pread 改变了文件偏移量 (%d -> %d)\n", 
               offset_before, offset_after);
    }
    
    printf("pread 读取内容: '%s'\n", buf);
    if (buf[0] == '0' && buf[1] == '1' && buf[2] == '2' && buf[3] == '3') {
        printf("PASS: pread 读取位置正确\n");
    } else {
        printf("FAIL: pread 读取位置错误\n");
    }
    
    close(fd);
    unlink(TEST_FILE);
    printf("\n");
}

void test_concurrent_simulation(void)
{
    printf("=== Test 3: 模拟多线程并发访问 ===\n");
    
    int fd = open(TEST_FILE, O_CREATE | O_RDWR);
    if (fd < 0) {
        printf("FAIL: 无法创建测试文件\n");
        exit(1);
    }
    
    // 初始化文件内容 (32 字节)
    char init[32];
    memset(init, '.', 32);
    write(fd, init, 32);
    
    // 模拟多个"线程"使用 pwrite 在不同位置写入
    // 由于使用 pwrite，它们不会相互干扰
    pwrite(fd, "AAAA", 4, 0);   // "线程 1" 写入位置 0
    pwrite(fd, "BBBB", 4, 8);   // "线程 2" 写入位置 8
    pwrite(fd, "CCCC", 4, 16);  // "线程 3" 写入位置 16
    pwrite(fd, "DDDD", 4, 24);  // "线程 4" 写入位置 24
    
    // 验证各个位置的数据
    char buf[8];
    int success = 1;
    
    memset(buf, 0, sizeof(buf));
    pread(fd, buf, 4, 0);
    if (strcmp(buf, "AAAA") != 0) {
        printf("FAIL: 位置 0 数据错误, 期望 'AAAA', 得到 '%s'\n", buf);
        success = 0;
    }
    
    memset(buf, 0, sizeof(buf));
    pread(fd, buf, 4, 8);
    if (strcmp(buf, "BBBB") != 0) {
        printf("FAIL: 位置 8 数据错误, 期望 'BBBB', 得到 '%s'\n", buf);
        success = 0;
    }
    
    memset(buf, 0, sizeof(buf));
    pread(fd, buf, 4, 16);
    if (strcmp(buf, "CCCC") != 0) {
        printf("FAIL: 位置 16 数据错误, 期望 'CCCC', 得到 '%s'\n", buf);
        success = 0;
    }
    
    memset(buf, 0, sizeof(buf));
    pread(fd, buf, 4, 24);
    if (strcmp(buf, "DDDD") != 0) {
        printf("FAIL: 位置 24 数据错误, 期望 'DDDD', 得到 '%s'\n", buf);
        success = 0;
    }
    
    if (success) {
        printf("PASS: 模拟并发访问成功，各位置数据正确\n");
    }
    
    close(fd);
    unlink(TEST_FILE);
    printf("\n");
}

void test_edge_cases(void)
{
    printf("=== Test 4: 边界情况测试 ===\n");
    
    int fd = open(TEST_FILE, O_CREATE | O_RDWR);
    if (fd < 0) {
        printf("FAIL: 无法创建测试文件\n");
        exit(1);
    }
    
    // 写入初始数据
    write(fd, "HELLO", 5);
    
    // 测试 1: pread 超出文件末尾
    char buf[32];
    memset(buf, 0, sizeof(buf));
    int ret = pread(fd, buf, 10, 3);
    printf("pread(10 字节, 偏移 3) 从 5 字节文件: 返回 %d\n", ret);
    if (ret == 2) {  // 只能读取 2 字节 (位置 3-4)
        printf("PASS: pread 正确处理文件末尾\n");
    } else {
        printf("INFO: pread 返回 %d (可能的实现差异)\n", ret);
    }
    
    // 测试 2: pread 完全超出文件
    memset(buf, 0, sizeof(buf));
    ret = pread(fd, buf, 5, 100);
    printf("pread(5 字节, 偏移 100) 从 5 字节文件: 返回 %d\n", ret);
    if (ret == 0 || ret == -1) {
        printf("PASS: pread 正确处理超出文件范围\n");
    }
    
    // 测试 3: 负偏移
    ret = pread(fd, buf, 5, -1);
    printf("pread 负偏移: 返回 %d\n", ret);
    if (ret == -1) {
        printf("PASS: pread 正确拒绝负偏移\n");
    }
    
    // 测试 4: pwrite 扩展文件
    ret = pwrite(fd, "END", 3, 10);
    printf("pwrite(3 字节, 偏移 10) 到 5 字节文件: 返回 %d\n", ret);
    
    struct stat st;
    fstat(fd, &st);
    printf("写入后文件大小: %d\n", (int)st.size);
    if (st.size >= 13) {
        printf("PASS: pwrite 可以扩展文件\n");
    }
    
    close(fd);
    unlink(TEST_FILE);
    printf("\n");
}

void test_large_offset(void)
{
    printf("=== Test 5: 大偏移测试 ===\n");
    
    int fd = open(TEST_FILE, O_CREATE | O_RDWR);
    if (fd < 0) {
        printf("FAIL: 无法创建测试文件\n");
        exit(1);
    }
    
    // 在较大偏移处写入数据
    int large_offset = 4096;  // 1 个块大小
    char *data = "SPARSE";
    int ret = pwrite(fd, data, 6, large_offset);
    printf("pwrite 到偏移 %d: 返回 %d\n", large_offset, ret);
    
    if (ret == 6) {
        // 验证读回
        char buf[16];
        memset(buf, 0, sizeof(buf));
        ret = pread(fd, buf, 6, large_offset);
        if (ret == 6 && strcmp(buf, "SPARSE") == 0) {
            printf("PASS: 大偏移读写成功\n");
        } else {
            printf("FAIL: 大偏移读回失败\n");
        }
    } else {
        printf("FAIL: 大偏移写入失败\n");
    }
    
    close(fd);
    unlink(TEST_FILE);
    printf("\n");
}

int main(int argc, char *argv[])
{
    printf("========================================\n");
    printf("   pread/pwrite 系统调用测试\n");
    printf("========================================\n\n");
    
    test_basic_pwrite();
    test_basic_pread();
    test_concurrent_simulation();
    test_edge_cases();
    test_large_offset();
    
    printf("========================================\n");
    printf("   所有测试完成\n");
    printf("========================================\n");
    
    exit(0);
}
