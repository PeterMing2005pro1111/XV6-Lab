#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

char*
get_filename(char *path)
{
  char *p;
  //从后往前找最后一个'/'
  for(p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  p++;
  return p;
}

void
find(char *path, char *target)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  // 打开路径（和 ls.c 完全一致）
  if((fd = open(path, 0)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }

  // 获取文件状态（和 ls.c 完全一致）
  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  // 判断类型并处理
  switch(st.type){
  case T_FILE:
    // 如果当前路径是文件：对比文件名，匹配成功就打印路径
    if(strcmp(get_filename(path), target) == 0){
      printf("%s\n", path);
    }
    break;
  case T_DIR:
    // 如果是目录：检查路径长度，防止溢出（和 ls.c 完全一致）
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf)
    {
      printf("find: path too long\n");
      break;
    }
    // 拼接路径前缀 "path/"
    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';

    // 循环读取目录项 dirent（和 ls.c 完全一致）
    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;

      //跳过 "." 和 ".."，防止陷入无限死循环
      if(strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0)
        continue;

      // 拼接完整子路径 "path/filename"
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0; 

      //递归调用自己，深入子目录寻找！
      find(buf, target);
    }
    break;
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  if(argc != 3){
    fprintf(2, "Usage: find <path> <filename>\n");
    exit(1);
  }

  find(argv[1], argv[2]);
  exit(0);
}