// xattrtest.c - 文件扩展属性功能测试
// 测试 setxattr, getxattr, listxattr, removexattr 系统调用

#include "types.h"
#include "stat.h"
#include "user/user.h"
#include "fcntl.h"

#define TESTFILE "xattrtest_file"

void test_setget_basic(void)
{
  int fd;
  char value[64];
  int ret;
  
  printf("=== 测试1: 设置和获取扩展属性 ===\n");
  
  fd = open(TESTFILE, O_RDWR | O_CREATE);
  if(fd < 0) {
    printf("FAIL: 无法创建测试文件\n");
    exit(1);
  }
  write(fd, "test", 4);
  close(fd);
  
  // 设置属性
  if(setxattr(TESTFILE, "user.author", "alice", 5) < 0) {
    printf("FAIL: setxattr 失败\n");
    exit(1);
  }
  printf("  设置属性 user.author = \"alice\"\n");
  
  // 获取属性
  memset(value, 0, sizeof(value));
  ret = getxattr(TESTFILE, "user.author", value, sizeof(value));
  if(ret < 0) {
    printf("FAIL: getxattr 失败\n");
    exit(1);
  }
  if(ret != 5 || memcmp(value, "alice", 5) != 0) {
    printf("FAIL: 属性值不匹配，期望 alice，实际 %s\n", value);
    exit(1);
  }
  printf("  获取属性成功: user.author = \"%s\" (长度=%d)\n", value, ret);
  
  printf("PASS: 设置和获取扩展属性测试通过\n\n");
}

void test_multiple_attrs(void)
{
  char value[64];
  int ret;
  
  printf("=== 测试2: 多个扩展属性 ===\n");
  
  // 设置多个属性
  if(setxattr(TESTFILE, "user.tag1", "important", 9) < 0) {
    printf("FAIL: 设置 tag1 失败\n");
    exit(1);
  }
  if(setxattr(TESTFILE, "user.tag2", "backup", 6) < 0) {
    printf("FAIL: 设置 tag2 失败\n");
    exit(1);
  }
  printf("  设置多个属性: tag1, tag2\n");
  
  // 验证各属性
  memset(value, 0, sizeof(value));
  ret = getxattr(TESTFILE, "user.tag1", value, sizeof(value));
  if(ret != 9 || memcmp(value, "important", 9) != 0) {
    printf("FAIL: tag1 值不匹配\n");
    exit(1);
  }
  
  memset(value, 0, sizeof(value));
  ret = getxattr(TESTFILE, "user.tag2", value, sizeof(value));
  if(ret != 6 || memcmp(value, "backup", 6) != 0) {
    printf("FAIL: tag2 值不匹配\n");
    exit(1);
  }
  printf("  验证多个属性成功\n");
  
  printf("PASS: 多个扩展属性测试通过\n\n");
}

void test_listxattr(void)
{
  char list[256];
  int ret;
  
  printf("=== 测试3: 列出所有扩展属性 ===\n");
  
  // 先查询需要的缓冲区大小
  ret = listxattr(TESTFILE, 0, 0);
  if(ret < 0) {
    printf("FAIL: listxattr 查询大小失败\n");
    exit(1);
  }
  printf("  属性列表总长度: %d 字节\n", ret);
  
  // 获取属性列表
  memset(list, 0, sizeof(list));
  ret = listxattr(TESTFILE, list, sizeof(list));
  if(ret < 0) {
    printf("FAIL: listxattr 获取列表失败\n");
    exit(1);
  }
  
  // 打印属性名（以 null 分隔）
  printf("  属性列表: ");
  int offset = 0;
  while(offset < ret) {
    printf("\"%s\" ", list + offset);
    offset += strlen(list + offset) + 1;
  }
  printf("\n");
  
  printf("PASS: 列出扩展属性测试通过\n\n");
}

void test_removexattr(void)
{
  char value[64];
  int ret;
  
  printf("=== 测试4: 删除扩展属性 ===\n");
  
  // 删除一个属性
  if(removexattr(TESTFILE, "user.tag1") < 0) {
    printf("FAIL: removexattr 失败\n");
    exit(1);
  }
  printf("  删除属性 user.tag1\n");
  
  // 尝试获取已删除的属性（应失败）
  ret = getxattr(TESTFILE, "user.tag1", value, sizeof(value));
  if(ret >= 0) {
    printf("FAIL: 删除后仍能获取属性\n");
    exit(1);
  }
  printf("  验证属性已删除（获取失败，符合预期）\n");
  
  // 其他属性仍存在
  memset(value, 0, sizeof(value));
  ret = getxattr(TESTFILE, "user.tag2", value, sizeof(value));
  if(ret < 0) {
    printf("FAIL: 其他属性不应受影响\n");
    exit(1);
  }
  printf("  其他属性 user.tag2 仍存在\n");
  
  printf("PASS: 删除扩展属性测试通过\n\n");
}

void test_update_attr(void)
{
  char value[64];
  int ret;
  
  printf("=== 测试5: 更新扩展属性 ===\n");
  
  // 更新现有属性
  if(setxattr(TESTFILE, "user.author", "bob", 3) < 0) {
    printf("FAIL: 更新属性失败\n");
    exit(1);
  }
  printf("  更新 user.author = \"bob\"\n");
  
  memset(value, 0, sizeof(value));
  ret = getxattr(TESTFILE, "user.author", value, sizeof(value));
  if(ret != 3 || memcmp(value, "bob", 3) != 0) {
    printf("FAIL: 更新后值不匹配\n");
    exit(1);
  }
  printf("  验证更新成功: user.author = \"%s\"\n", value);
  
  printf("PASS: 更新扩展属性测试通过\n\n");
}

void test_nonexistent_attr(void)
{
  char value[64];
  int ret;
  
  printf("=== 测试6: 获取不存在的属性 ===\n");
  
  ret = getxattr(TESTFILE, "user.nonexistent", value, sizeof(value));
  if(ret >= 0) {
    printf("FAIL: 获取不存在的属性应失败\n");
    exit(1);
  }
  printf("  获取不存在的属性返回错误（符合预期）\n");
  
  printf("PASS: 获取不存在属性测试通过\n\n");
}

int main(int argc, char *argv[])
{
  printf("\n========== 文件扩展属性（xattr）功能测试 ==========\n\n");
  
  test_setget_basic();
  test_multiple_attrs();
  test_listxattr();
  test_removexattr();
  test_update_attr();
  test_nonexistent_attr();
  
  // 清理测试文件
  unlink(TESTFILE);
  
  printf("========== 所有测试通过! ==========\n");
  exit(0);
}
