// accesstest.c - access 文件权限检查测试

#include "kernel/include/types.h"
#include "kernel/include/stat.h"
#include "user/user.h"
#include "kernel/include/fcntl.h"

#define TEST_FILE "/access_test"

void test_file_exists(void)
{
    printf("=== Test 1: 文件存在检查 (F_OK) ===\n");
    
    // 文件不存在
    int ret = access("/nonexistent_file", F_OK);
    if (ret == -1) {
        printf("PASS: 不存在的文件返回 -1\n");
    } else {
        printf("FAIL: 不存在的文件应返回 -1, 实际 %d\n", ret);
    }
    
    // 创建文件
    int fd = open(TEST_FILE, O_CREATE | O_RDWR);
    close(fd);
    
    // 文件存在
    ret = access(TEST_FILE, F_OK);
    if (ret == 0) {
        printf("PASS: 存在的文件返回 0\n");
    } else {
        printf("FAIL: 存在的文件应返回 0, 实际 %d\n", ret);
    }
    
    unlink(TEST_FILE);
    printf("\n");
}

void test_read_permission(void)
{
    printf("=== Test 2: 读权限检查 (R_OK) ===\n");
    
    // 创建文件并设置权限
    int fd = open(TEST_FILE, O_CREATE | O_RDWR);
    close(fd);
    
    // 默认应该有读权限
    int ret = access(TEST_FILE, R_OK);
    printf("access(R_OK) 返回: %d\n", ret);
    
    // 设置只有写权限
    chmod(TEST_FILE, 2);  // 只写
    ret = access(TEST_FILE, R_OK);
    printf("移除读权限后 access(R_OK) 返回: %d\n", ret);
    
    // 恢复读权限
    chmod(TEST_FILE, 6);  // 读写
    ret = access(TEST_FILE, R_OK);
    printf("恢复读权限后 access(R_OK) 返回: %d\n", ret);
    
    unlink(TEST_FILE);
    printf("\n");
}

void test_write_permission(void)
{
    printf("=== Test 3: 写权限检查 (W_OK) ===\n");
    
    int fd = open(TEST_FILE, O_CREATE | O_RDWR);
    close(fd);
    
    // 默认应该有写权限
    int ret = access(TEST_FILE, W_OK);
    printf("access(W_OK) 返回: %d\n", ret);
    
    // 设置只读权限
    chmod(TEST_FILE, 4);  // 只读
    ret = access(TEST_FILE, W_OK);
    printf("设置只读后 access(W_OK) 返回: %d (期望 -1)\n", ret);
    
    // 恢复写权限
    chmod(TEST_FILE, 6);  // 读写
    
    unlink(TEST_FILE);
    printf("\n");
}

void test_multiple_permissions(void)
{
    printf("=== Test 4: 多权限组合检查 ===\n");
    
    int fd = open(TEST_FILE, O_CREATE | O_RDWR);
    close(fd);
    
    // 设置读写权限
    chmod(TEST_FILE, 6);  // rw-
    
    // 检查读和写权限
    int ret = access(TEST_FILE, R_OK | W_OK);
    printf("access(R_OK | W_OK) 返回: %d (期望 0)\n", ret);
    
    // 设置只读
    chmod(TEST_FILE, 4);  // r--
    ret = access(TEST_FILE, R_OK | W_OK);
    printf("只读时 access(R_OK | W_OK) 返回: %d (期望 -1)\n", ret);
    
    unlink(TEST_FILE);
    printf("\n");
}

void test_directory_access(void)
{
    printf("=== Test 5: 目录访问检查 ===\n");
    
    // 检查根目录
    int ret = access("/", F_OK);
    if (ret == 0) {
        printf("PASS: 根目录存在检查返回 0\n");
    } else {
        printf("FAIL: 根目录存在检查返回 %d\n", ret);
    }
    
    // 创建并检查目录
    mkdir("/access_test_dir");
    ret = access("/access_test_dir", F_OK);
    if (ret == 0) {
        printf("PASS: 新目录存在检查返回 0\n");
    } else {
        printf("FAIL: 新目录存在检查返回 %d\n", ret);
    }
    
    // 清理 - 目录需要为空才能删除
    unlink("/access_test_dir");
    printf("\n");
}

int main(int argc, char *argv[])
{
    printf("========================================\n");
    printf("   access 文件权限检查测试\n");
    printf("========================================\n\n");
    
    test_file_exists();
    test_read_permission();
    test_write_permission();
    test_multiple_permissions();
    test_directory_access();
    
    printf("========================================\n");
    printf("   所有测试完成\n");
    printf("========================================\n");
    
    exit(0);
}
