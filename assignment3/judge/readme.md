## Grading

### 使用

解压后将 `sh` 文件夹和 `grade.sh` 复制到项目根目录即可。

**注意**：**使用之前请将 `grade.sh` 中 llvm 改为你自己环境对应的路径**。

```shell
cmake –DLLVM_DIR=/usr/local/llvm10d -DCMAKE_BUILD_TYPE=Debug ../.
```

运行：

```shell
source grade.sh
```

### 文件清单

`sh/compile.sh`：生成字节码

`sh/extract.py`：提取注释作为 ground truth

`sh/gen_truth.sh`：调用 `sh/extract.py` 

`sh/format_helper.py`：格式转换，可以不用管