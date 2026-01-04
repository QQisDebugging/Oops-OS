// VFS 和 mount/umount 测试程序
#include "types.h"
#include "stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
  printf("=== VFS mount/umount 测试 ===\n\n");

  // 测试1: 尝试挂载 FAT 文件系统（预期失败，因为没有实际的 FAT 设备）
  printf("测试1: 尝试挂载 FAT 文件系统到 /mnt...\n");
  int ret = mount("/dev/disk1", "/mnt", "fat");
  if (ret < 0) {
    printf("  挂载失败 (预期结果，当前没有 FAT 设备)\n");
  } else {
    printf("  挂载成功\n");
  }
  printf("\n");

  // 测试2: 无效的文件系统类型
  printf("测试2: 尝试挂载无效的文件系统类型...\n");
  ret = mount("/dev/disk1", "/mnt", "unknown");
  if (ret < 0) {
    printf("  挂载失败 (预期结果，无效的文件系统类型)\n");
  } else {
    printf("  错误: 不应该成功\n");
  }
  printf("\n");

  // 测试3: 卸载未挂载的路径
  printf("测试3: 尝试卸载未挂载的路径...\n");
  ret = umount("/mnt");
  if (ret < 0) {
    printf("  卸载失败 (预期结果，该路径未挂载)\n");
  } else {
    printf("  错误: 不应该成功\n");
  }
  printf("\n");

  // 测试4: 尝试卸载根文件系统
  printf("测试4: 尝试卸载根文件系统...\n");
  ret = umount("/");
  if (ret < 0) {
    printf("  卸载失败 (预期结果，不能卸载根文件系统)\n");
  } else {
    printf("  错误: 不应该成功\n");
  }
  printf("\n");

  printf("=== VFS 测试完成 ===\n");
  printf("\n说明:\n");
  printf("VFS (Virtual File System) 是一个抽象层，允许不同的文件系统\n");
  printf("（如 xv6 原生文件系统、FAT 等）通过统一的接口被访问。\n");
  printf("当前支持的文件系统类型: xv6, fat\n");
  printf("\nFAT 文件系统需要实际的 FAT 格式设备才能挂载成功。\n");
  printf("xv6 原生文件系统已作为根文件系统自动挂载在 / 路径。\n");

  exit(0);
}
