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
         |   ├── LLVMAssignment.cpp # 作业需要提交的文件
         |   └── CMakeLists.txt # 非必要不要修改
         ├── gradeForDocker.sh
         ├── ground-truth # 作业答案目录
         ├── extract.py # 辅助的python文件
         ├── format_helper.py # 辅助的python文件
         ├── compile.sh # 预先运行将每个测试用例生成 .bc文件于bc目录下
         ├── bc # compile脚本自动构建的目录，用于存放.bc文件
         └── test
             ├── test00.c
             └── ...
    ```
- 注意此脚本是进入docker容器内进行运行, 主机环境请用`docker exec`调用该脚本。

**第二次作业提交时候，请只提交`LLVMAssignment.cpp`这个文件，不要打包（即提交两个单独的文件），如果有其他的`.h`或`.cpp`文件同样单独添加，也不要提交其他测试文件、可执行程序等等，如果`CMakeLists.txt`文件有修改请一并提交**

### 主机环境
- 请在 `grade.sh` 修改对应的 llvm 的路径
- 你的目录应该如下：
    ```shell
    ├── LLVMAssignment.cpp
    ├── build   #脚本自动构建
    ├── CMakeLists.txt
    ├── grade.sh
    ├── ground-truth # 作业答案目录
    ├── extract.py # 辅助的python文件
    ├── format_helper.py # 辅助的python文件
    ├── compile.sh # 预先运行将每个测试用例生成 .bc文件于bc目录下
    ├── bc # compile脚本自动构建的目录，用于存放.bc文件
    └── test
    ```

## 运行

直接运行 `grade.sh` 即可。

```shell
source grade.sh
```

- `ground-truth` 是答案，用 `extract.py` 提取注释后手工修改得到。

- `compile.sh` 会在 `bc` 目录下生成 `.bc` 文件。

- `format_helper.py` 用于格式化，方便进行结果比对。

### 其他
