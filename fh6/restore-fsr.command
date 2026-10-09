#!/bin/zsh
cd -- "${0:A:h}" || exit 1
if ! command -v python3 >/dev/null 2>&1; then
  print '需要 Python 3.9 或更新版本。请先安装 Python 3，再重新打开此文件。'
  read '?按回车关闭…'
  exit 1
fi
python3 configure.py setup --resolution 2560x1600 --apply
result=$?
read '?按回车关闭…'
exit "$result"
