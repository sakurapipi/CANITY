"""打包发布：编译 -> 嵌资源 -> 生成可直接发布的 zip。

为什么需要它：
  直接把 exe 甩给别人，Windows 会在对方电脑上弹
  「Windows 已保护你的电脑」或「打开文件 - 安全警告」。
  原因是文件带 MOTW（Mark of the Web）—— 从浏览器/微信/QQ 下载的文件
  会被打上"来自 Internet"的标记，系统跑它之前先拦一道。
  这个脚本没法让标记消失（那是下载通道加的），但能做两件正确的事：
    ① exe 自带图标 / 版本信息 / 清单，不会像个"三无程序"
    ② 压缩包里附一个「解除锁定.bat」，玩家双击一次就把标记去掉

用法：
    python tools/package_release.py
产物：
    dist/CANITY_v<版本>_win64.zip
"""
import os
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIST = os.path.join(ROOT, "dist")

# 可选的本地工具链目录。
# 默认只用 PATH 里的 gcc/windres（CI 和正常环境都够用）；
# 需要指向别处的 w64devkit 时设环境变量 CANITY_TOOLCHAIN，别把本机路径写死进仓库。
LOCAL_BIN = os.environ.get("CANITY_TOOLCHAIN", "")


def tool(name: str) -> str:
    exe = name + ".exe" if os.name == "nt" else name
    local = os.path.join(LOCAL_BIN, exe)
    if os.path.exists(local):
        return local
    found = shutil.which(name)
    if not found:
        sys.exit("找不到 %s（装个 MinGW 或者设个 PATH）" % name)
    return found


def version() -> str:
    """从 common.h 里读版本号，别在两处各写一遍。"""
    with open(os.path.join(ROOT, "include", "common.h"), encoding="utf-8") as f:
        m = re.search(r'#define\s+CANITY_VERSION_STR\s+"([^"]+)"', f.read())
    return m.group(1) if m else "0.0.0"


def run(cmd) -> None:
    print("  $ " + " ".join(cmd))
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if p.returncode != 0:
        print(p.stdout)
        print(p.stderr)
        sys.exit("编译失败")
    if p.stderr.strip():
        for line in p.stderr.splitlines():
            if "warning" in line and "entity.c" not in line:
                print("  " + line)


READ_ME_SRC = os.path.join(ROOT, "docs", "package_readme.txt")
UNBLOCK_SRC = os.path.join(ROOT, "docs", "解除锁定.bat")


def copy_extra(out_dir: str, ver: str) -> None:
    """说明和解除锁定脚本都放在 docs/ 下 —— 本地脚本和 CI 用同一份，
    不要在两处各写一遍（改了一处忘了另一处，发出去的文件就会对不上）。"""
    with open(READ_ME_SRC, encoding="utf-8-sig") as f:
        readme = f.read().replace("%VER%", ver)
    # 记事本需要 BOM 才不乱码
    with open(os.path.join(out_dir, "第一次运行请看.txt"), "w",
              encoding="utf-8-sig", newline="\r\n") as f:
        f.write(readme)
    shutil.copyfile(UNBLOCK_SRC, os.path.join(out_dir, "解除锁定.bat"))


def main() -> int:
    ver = version()
    name = "CANITY_v%s_win64" % ver
    out_dir = os.path.join(DIST, name)

    if os.path.isdir(DIST):
        shutil.rmtree(DIST)
    os.makedirs(out_dir)

    print("1/4 资源")
    obj = os.path.join(ROOT, "res", "canity_res.o")
    run([tool("windres"), os.path.join("res", "canity.rc"), "-O", "coff", "-o", obj])

    print("2/4 编译")
    sources = [os.path.join("src", f) for f in sorted(os.listdir(os.path.join(ROOT, "src")))
               if f.endswith(".c")]
    exe = os.path.join(out_dir, "canity.exe")
    run([tool("gcc")] + sources + [obj, "-Iinclude", "-o", exe, "-O2", "-Wall", "-Wextra",
                                   "-lgdi32", "-luser32", "-lwinmm", "-mwindows"])

    print("3/4 附带文件")
    copy_extra(out_dir, ver)

    print("4/4 打 zip")
    zip_path = os.path.join(DIST, name + ".zip")
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for fn in sorted(os.listdir(out_dir)):
            z.write(os.path.join(out_dir, fn), os.path.join(name, fn))

    # 自检：清单和版本信息必须真的嵌进去了
    data = open(os.path.join(out_dir, "canity.exe"), "rb").read()
    ok_manifest = b"requestedExecutionLevel" in data and b"asInvoker" in data
    ok_version = "Canity - raycasting horror game".encode("utf-16-le") in data
    print("   exe %.0f KB | 清单 %s | 版本信息 %s"
          % (len(data) / 1024.0, "OK" if ok_manifest else "缺失!", "OK" if ok_version else "缺失!"))
    print("   zip %.0f KB -> %s" % (os.path.getsize(zip_path) / 1024.0, zip_path))
    if not (ok_manifest and ok_version):
        print("   警告：资源没嵌全，检查 res/canity.rc")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
