## 评测脚本
评测脚本分为两个`grade.sh`和`gradeForDocker.sh`适用于两种环境。

### docker环境
- 请参考`gradeForDocker.sh`中的常量将必须的文件导入docker容器内
- 你的容器目录应该如下：
    ```shell
     /
     ├── build  #脚本自动构建
     └── root
         ├── src
         |   ├── ASTInterpreter.cpp # 作业需要提交的文件
         |   ├── CMakeLists.txt # 非必要不要修改
         |   └── Environment.h # 作业需要提交的文件
         ├── gradeForDocker.sh
         ├── lib
         │   └── builtin.c
         └── test
             ├── test00.c
             └── ...
    ```
- 注意此脚本是进入docker容器内进行运行, 主机环境请用`docker exec`调用该脚本。

**第一次作业提交时候，请只提交`ASTInterpreter.cpp`和`Environment.h`这两个文件，不要打包（即提交两个单独的文件），如果有其他的`.h`或`.cpp`文件同样单独添加，也不要提交其他测试文件、可执行程序等等**

### 主机环境
- 请在 `grade.sh` 修改对应的 clang 和 llvm 的路径
- 你的目录应该如下：
    ```shell
    ├── ASTInterpreter.cpp
    ├── build
    ├── CMakeLists.txt
    ├── Environment.h
    ├── grade.sh
    ├── lib
    │   └── builtin.c
    └── test
    ```