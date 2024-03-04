import subprocess
import logging
import os
import urllib.parse
import zipfile
import tarfile
import shlex
import shutil
import platform
import multiprocessing
from typing import Callable, NamedTuple, Optional, List, Union, Dict


logging.basicConfig(level=logging.DEBUG)


class ChangeDirectory(object):
    def __init__(self, cwd):
        self._cwd = cwd

    def __enter__(self):
        self._old_cwd = os.getcwd()
        logging.debug(f'pushd {self._old_cwd} --> {self._cwd}')
        os.chdir(self._cwd)

    def __exit__(self, exctype, excvalue, trace):
        logging.debug(f'popd {self._old_cwd} <-- {self._cwd}')
        os.chdir(self._old_cwd)
        return False


def cd(cwd):
    return ChangeDirectory(cwd)


def cmd(args, **kwargs):
    logging.debug(f'+{args} {kwargs}')
    if 'check' not in kwargs:
        kwargs['check'] = True
    if 'resolve' in kwargs:
        resolve = kwargs['resolve']
        del kwargs['resolve']
    else:
        resolve = True
    if resolve:
        args = [shutil.which(args[0]), *args[1:]]
    return subprocess.run(args, **kwargs)


# 標準出力をキャプチャするコマンド実行。シェルの `cmd ...` や $(cmd ...) と同じ
def cmdcap(args, **kwargs):
    # 3.7 でしか使えない
    # kwargs['capture_output'] = True
    kwargs['stdout'] = subprocess.PIPE
    kwargs['stderr'] = subprocess.PIPE
    kwargs['encoding'] = 'utf-8'
    return cmd(args, **kwargs).stdout.strip()


def rm_rf(path: str):
    if not os.path.exists(path):
        logging.debug(f'rm -rf {path} => path not found')
        return
    if os.path.isfile(path) or os.path.islink(path):
        os.remove(path)
        logging.debug(f'rm -rf {path} => file removed')
    if os.path.isdir(path):
        shutil.rmtree(path)
        logging.debug(f'rm -rf {path} => directory removed')


def mkdir_p(path: str):
    if os.path.exists(path):
        logging.debug(f'mkdir -p {path} => already exists')
        return
    os.makedirs(path, exist_ok=True)
    logging.debug(f'mkdir -p {path} => directory created')


if platform.system() == 'Windows':
    PATH_SEPARATOR = ';'
else:
    PATH_SEPARATOR = ':'


def add_path(path: str, is_after=False):
    logging.debug(f'add_path: {path}')
    if 'PATH' not in os.environ:
        os.environ['PATH'] = path
        return

    if is_after:
        os.environ['PATH'] = os.environ['PATH'] + PATH_SEPARATOR + path
    else:
        os.environ['PATH'] = path + PATH_SEPARATOR + os.environ['PATH']


def download(url: str, output_dir: Optional[str] = None, filename: Optional[str] = None) -> str:
    if filename is None:
        output_path = urllib.parse.urlparse(url).path.split('/')[-1]
    else:
        output_path = filename

    if output_dir is not None:
        output_path = os.path.join(output_dir, output_path)

    if os.path.exists(output_path):
        return output_path

    try:
        if shutil.which('curl') is not None:
            cmd(["curl", "-fLo", output_path, url])
        else:
            cmd(["wget", "-cO", output_path, url])
    except Exception:
        # ゴミを残さないようにする
        if os.path.exists(output_path):
            os.remove(output_path)
        raise

    return output_path


def read_version_file(path: str) -> Dict[str, str]:
    versions = {}

    lines = open(path).readlines()
    for line in lines:
        line = line.strip()

        # コメント行
        if line[:1] == '#':
            continue

        # 空行
        if len(line) == 0:
            continue

        [a, b] = map(lambda x: x.strip(), line.split('=', 2))
        versions[a] = b.strip('"')

    return versions


# dir 以下にある全てのファイルパスを、dir2 からの相対パスで返す
def enum_all_files(dir, dir2):
    for root, _, files in os.walk(dir):
        for file in files:
            yield os.path.relpath(os.path.join(root, file), dir2)


def versioned(func):
    def wrapper(version, version_file, *args, **kwargs):
        if 'ignore_version' in kwargs:
            if kwargs.get('ignore_version'):
                rm_rf(version_file)
            del kwargs['ignore_version']

        if os.path.exists(version_file):
            ver = open(version_file).read()
            if ver.strip() == version.strip():
                return

        r = func(version=version, *args, **kwargs)

        with open(version_file, 'w') as f:
            f.write(version)

        return r

    return wrapper


# アーカイブが単一のディレクトリに全て格納されているかどうかを調べる。
#
# 単一のディレクトリに格納されている場合はそのディレクトリ名を返す。
# そうでない場合は None を返す。
def _is_single_dir(infos: List[Union[zipfile.ZipInfo, tarfile.TarInfo]],
                   get_name: Callable[[Union[zipfile.ZipInfo, tarfile.TarInfo]], str],
                   is_dir: Callable[[Union[zipfile.ZipInfo, tarfile.TarInfo]], bool]) -> Optional[str]:
    # tarfile: ['path', 'path/to', 'path/to/file.txt']
    # zipfile: ['path/', 'path/to/', 'path/to/file.txt']
    # どちらも / 区切りだが、ディレクトリの場合、後ろに / が付くかどうかが違う
    dirname = None
    for info in infos:
        name = get_name(info)
        n = name.rstrip('/').find('/')
        if n == -1:
            # ルートディレクトリにファイルが存在している
            if not is_dir(info):
                return None
            dir = name.rstrip('/')
        else:
            dir = name[0:n]
        # ルートディレクトリに２個以上のディレクトリが存在している
        if dirname is not None and dirname != dir:
            return None
        dirname = dir

    return dirname


def is_single_dir_tar(tar: tarfile.TarFile) -> Optional[str]:
    return _is_single_dir(tar.getmembers(), lambda t: t.name, lambda t: t.isdir())


def is_single_dir_zip(zip: zipfile.ZipFile) -> Optional[str]:
    return _is_single_dir(zip.infolist(), lambda z: z.filename, lambda z: z.is_dir())


# 解凍した上でファイル属性を付与する
def _extractzip(z: zipfile.ZipFile, path: str):
    z.extractall(path)
    if platform.system() == 'Windows':
        return
    for info in z.infolist():
        if info.is_dir():
            continue
        filepath = os.path.join(path, info.filename)
        mod = info.external_attr >> 16
        if (mod & 0o120000) == 0o120000:
            # シンボリックリンク
            with open(filepath, 'r') as f:
                src = f.read()
            os.remove(filepath)
            with cd(os.path.dirname(filepath)):
                if os.path.exists(src):
                    os.symlink(src, filepath)
        if os.path.exists(filepath):
            # 普通のファイル
            os.chmod(filepath, mod & 0o777)


# zip または tar.gz ファイルを展開する。
#
# 展開先のディレクトリは {output_dir}/{output_dirname} となり、
# 展開先のディレクトリが既に存在していた場合は削除される。
#
# もしアーカイブの内容が単一のディレクトリであった場合、
# そのディレクトリは無いものとして展開される。
#
# つまりアーカイブ libsora-1.23.tar.gz の内容が
# ['libsora-1.23', 'libsora-1.23/file1', 'libsora-1.23/file2']
# であった場合、extract('libsora-1.23.tar.gz', 'out', 'libsora') のようにすると
# - out/libsora/file1
# - out/libsora/file2
# が出力される。
#
# また、アーカイブ libsora-1.23.tar.gz の内容が
# ['libsora-1.23', 'libsora-1.23/file1', 'libsora-1.23/file2', 'LICENSE']
# であった場合、extract('libsora-1.23.tar.gz', 'out', 'libsora') のようにすると
# - out/libsora/libsora-1.23/file1
# - out/libsora/libsora-1.23/file2
# - out/libsora/LICENSE
# が出力される。
def extract(file: str, output_dir: str, output_dirname: str, filetype: Optional[str] = None):
    path = os.path.join(output_dir, output_dirname)
    logging.info(f"Extract {file} to {path}")
    if filetype == 'gzip' or file.endswith('.tar.gz'):
        rm_rf(path)
        with tarfile.open(file) as t:
            dir = is_single_dir_tar(t)
            if dir is None:
                os.makedirs(path, exist_ok=True)
                t.extractall(path)
            else:
                logging.info(f"Directory {dir} is stripped")
                path2 = os.path.join(output_dir, dir)
                rm_rf(path2)
                t.extractall(output_dir)
                if path != path2:
                    logging.debug(f"mv {path2} {path}")
                    os.replace(path2, path)
    elif filetype == 'zip' or file.endswith('.zip'):
        rm_rf(path)
        with zipfile.ZipFile(file) as z:
            dir = is_single_dir_zip(z)
            if dir is None:
                os.makedirs(path, exist_ok=True)
                # z.extractall(path)
                _extractzip(z, path)
            else:
                logging.info(f"Directory {dir} is stripped")
                path2 = os.path.join(output_dir, dir)
                rm_rf(path2)
                # z.extractall(output_dir)
                _extractzip(z, output_dir)
                if path != path2:
                    logging.debug(f"mv {path2} {path}")
                    os.replace(path2, path)
    else:
        raise Exception('file should end with .tar.gz or .zip')


def clone_and_checkout(url, version, dir, fetch, fetch_force):
    if fetch_force:
        rm_rf(dir)

    if not os.path.exists(os.path.join(dir, '.git')):
        cmd(['git', 'clone', url, dir])
        fetch = True

    if fetch:
        with cd(dir):
            cmd(['git', 'fetch'])
            cmd(['git', 'reset', '--hard'])
            cmd(['git', 'clean', '-df'])
            cmd(['git', 'checkout', '-f', version])


def git_clone_shallow(url, hash, dir):
    rm_rf(dir)
    mkdir_p(dir)
    with cd(dir):
        cmd(['git', 'init'])
        cmd(['git', 'remote', 'add', 'origin', url])
        cmd(['git', 'fetch', '--depth=1', 'origin', hash])
        cmd(['git', 'reset', '--hard', 'FETCH_HEAD'])


@versioned
def install_rootfs(version, install_dir, conf):
    rootfs_dir = os.path.join(install_dir, 'rootfs')
    rm_rf(rootfs_dir)
    cmd(['multistrap', '--no-auth', '-a', 'arm64', '-d', rootfs_dir, '-f', conf])
    # 絶対パスのシンボリックリンクを相対パスに置き換えていく
    for dir, _, filenames in os.walk(rootfs_dir):
        for filename in filenames:
            linkpath = os.path.join(dir, filename)
            # symlink かどうか
            if not os.path.islink(linkpath):
                continue
            target = os.readlink(linkpath)
            # 絶対パスかどうか
            if not os.path.isabs(target):
                continue
            # rootfs_dir を先頭に付けることで、
            # rootfs の外から見て正しい絶対パスにする
            targetpath = rootfs_dir + target
            # 参照先の絶対パスが存在するかどうか
            if not os.path.exists(targetpath):
                continue
            # 相対パスに置き換える
            relpath = os.path.relpath(targetpath, dir)
            logging.debug(f'{linkpath[len(rootfs_dir):]} targets {target} to {relpath}')
            os.remove(linkpath)
            os.symlink(relpath, linkpath)

    # なぜかシンボリックリンクが登録されていないので作っておく
    link = os.path.join(rootfs_dir, 'usr', 'lib', 'aarch64-linux-gnu', 'tegra', 'libnvbuf_fdmap.so')
    file = os.path.join(rootfs_dir, 'usr', 'lib', 'aarch64-linux-gnu', 'tegra', 'libnvbuf_fdmap.so.1.0.0')
    if os.path.exists(file) and not os.path.exists(link):
        os.symlink(os.path.basename(file), link)


@versioned
def install_webrtc(version, source_dir, install_dir, platform: str):
    win = platform.startswith("windows_")
    filename = f'webrtc.{platform}.{"zip" if win else "tar.gz"}'
    rm_rf(os.path.join(source_dir, filename))
    archive = download(
        f'https://github.com/shiguredo-webrtc-build/webrtc-build/releases/download/{version}/{filename}',
        output_dir=source_dir)
    rm_rf(os.path.join(install_dir, 'webrtc'))
    extract(archive, output_dir=install_dir, output_dirname='webrtc')


class WebrtcInfo(NamedTuple):
    version_file: str
    webrtc_include_dir: str
    webrtc_library_dir: str
    clang_dir: str
    libcxx_dir: str


def get_webrtc_info(webrtcbuild: bool, source_dir: str, build_dir: str, install_dir: str) -> WebrtcInfo:
    webrtc_source_dir = os.path.join(source_dir, 'webrtc')
    webrtc_build_dir = os.path.join(build_dir, 'webrtc')
    webrtc_install_dir = os.path.join(install_dir, 'webrtc')

    if webrtcbuild:
        return WebrtcInfo(
            version_file=os.path.join(source_dir, 'webrtc-build', 'VERSION'),
            webrtc_include_dir=os.path.join(webrtc_source_dir, 'src'),
            webrtc_library_dir=os.path.join(webrtc_build_dir, 'obj')
            if platform.system() == 'Windows' else webrtc_build_dir, clang_dir=os.path.join(
                webrtc_source_dir, 'src', 'third_party', 'llvm-build', 'Release+Asserts'),
            libcxx_dir=os.path.join(webrtc_source_dir, 'src', 'buildtools', 'third_party', 'libc++', 'src'),)
    else:
        return WebrtcInfo(
            version_file=os.path.join(webrtc_install_dir, 'VERSIONS'),
            webrtc_include_dir=os.path.join(webrtc_install_dir, 'include'),
            webrtc_library_dir=os.path.join(install_dir, 'webrtc', 'lib'),
            clang_dir=os.path.join(install_dir, 'llvm', 'clang'),
            libcxx_dir=os.path.join(install_dir, 'llvm', 'libcxx'),
        )


class SoraInfo(NamedTuple):
    sora_install_dir: str
    boost_install_dir: str
    lyra_install_dir: str


def install_sora_and_deps(platform: str, source_dir:str, build_dir:str, install_dir: str):
    version = read_version_file('VERSION')

    # Boost
    install_boost_args = {
        'version': version['BOOST_VERSION'],
        'version_file': os.path.join(install_dir, 'boost.version'),
        'source_dir': source_dir,
        'install_dir': install_dir,
        'sora_version': version['SORA_CPP_SDK_VERSION'],
        'platform': platform,
    }
    install_boost(**install_boost_args)

    # Lyra
    install_lyra_args = {
        'version': version['LYRA_VERSION'],
        'version_file': os.path.join(install_dir, 'lyra.version'),
        'source_dir': source_dir,
        'install_dir': install_dir,
        'sora_version': version['SORA_CPP_SDK_VERSION'],
        'platform': platform,
    }
    install_lyra(**install_lyra_args)

    # Sora C++ SDK
    install_sora_args = {
        'version': version['SORA_CPP_SDK_VERSION'],
        'version_file': os.path.join(install_dir, 'sora.version'),
        'source_dir': source_dir,
        'install_dir': install_dir,
        'platform': platform,
    }
    install_sora(**install_sora_args)



# 内部で os.path.abspath() を利用しており、 os.path.abspath() はカレントディレクトリに依存するため、
# この関数を利用する場合は ArgumentParser.parse_args() 実行前にカレントディレクトリを変更してはならない
# 
# また、 --sora-args の指定には `--sora-args='--test'` のように `=` を使う必要がある
# `--sora-args '--test'` のようにスペースを使うと、ハイフンから始まるオプションが正しく解釈されない
def add_sora_arguments(parser):
    parser.add_argument("--sora-dir", type=os.path.abspath, default=None,
                        help="Refer to local Sora C++ SDK. "
                        "When this option is specified, Sora C++ SDK will also be built.")
    parser.add_argument("--sora-args", type=shlex.split, default=[],
                        help="Options for building local Sora C++ SDK when `--sora-dir` is specified.")


def build_sora(platform: str, sora_dir: str, sora_args: List[str], debug: bool):
    if debug and '--debug' not in sora_args:
        sora_args = ['--debug', *sora_args]

    with cd(sora_dir):
        cmd(['python3', 'run.py', platform, *sora_args])


def get_sora_info(install_dir: str, sora_dir: Optional[str], platform: str, debug: bool) -> SoraInfo:
    if sora_dir is not None:
        configuration = 'debug' if debug else 'release'
        install_dir = os.path.join(sora_dir, '_install', platform, configuration)

    return SoraInfo(
        sora_install_dir = os.path.join(install_dir, 'sora'),
        boost_install_dir = os.path.join(install_dir, 'boost'),
        lyra_install_dir = os.path.join(install_dir, 'lyra'),
    )


@versioned
def install_llvm(version, install_dir,
                 tools_url, tools_commit,
                 libcxx_url, libcxx_commit,
                 buildtools_url, buildtools_commit):
    llvm_dir = os.path.join(install_dir, 'llvm')
    rm_rf(llvm_dir)
    mkdir_p(llvm_dir)
    with cd(llvm_dir):
        # tools の update.py を叩いて特定バージョンの clang バイナリを拾う
        git_clone_shallow(tools_url, tools_commit, 'tools')
        with cd('tools'):
            cmd(['python3',
                os.path.join('clang', 'scripts', 'update.py'),
                '--output-dir', os.path.join(llvm_dir, 'clang')])

        # 特定バージョンの libcxx を利用する
        git_clone_shallow(libcxx_url, libcxx_commit, 'libcxx')

        # __config_site のために特定バージョンの buildtools を取得する
        git_clone_shallow(buildtools_url, buildtools_commit, 'buildtools')
        shutil.copyfile(os.path.join(llvm_dir, 'buildtools', 'third_party', 'libc++', '__config_site'),
                        os.path.join(llvm_dir, 'libcxx', 'include', '__config_site'))


@versioned
def install_boost(version, source_dir, install_dir, sora_version, platform: str):
    win = platform.startswith("windows_")
    filename = f'boost-{version}_sora-cpp-sdk-{sora_version}_{platform}.{"zip" if win else "tar.gz"}'
    rm_rf(os.path.join(source_dir, filename))
    archive = download(
        f'https://github.com/shiguredo/sora-cpp-sdk/releases/download/{sora_version}/{filename}',
        output_dir=source_dir)
    rm_rf(os.path.join(install_dir, 'boost'))
    extract(archive, output_dir=install_dir, output_dirname='boost')


@versioned
def install_lyra(version, source_dir, install_dir, sora_version, platform: str):
    win = platform.startswith("windows_")
    filename = f'lyra-{version}_sora-cpp-sdk-{sora_version}_{platform}.{"zip" if win else "tar.gz"}'
    rm_rf(os.path.join(source_dir, filename))
    archive = download(
        f'https://github.com/shiguredo/sora-cpp-sdk/releases/download/{sora_version}/{filename}',
        output_dir=source_dir)
    rm_rf(os.path.join(install_dir, 'lyra'))
    extract(archive, output_dir=install_dir, output_dirname='lyra')


def cmake_path(path: str) -> str:
    return path.replace('\\', '/')


@versioned
def install_cmake(version, source_dir, install_dir, platform: str, ext):
    url = f'https://github.com/Kitware/CMake/releases/download/v{version}/cmake-{version}-{platform}.{ext}'
    path = download(url, source_dir)
    extract(path, install_dir, 'cmake')
    # Android で自前の CMake を利用する場合、ninja へのパスが見つけられない問題があるので、同じディレクトリに symlink を貼る
    # https://issuetracker.google.com/issues/206099937
    if platform.startswith('linux'):
        with cd(os.path.join(install_dir, 'cmake', 'bin')):
            cmd(['ln', '-s', '/usr/bin/ninja', 'ninja'])


@versioned
def install_sdl2(version, source_dir, build_dir, install_dir, debug: bool, platform: str, cmake_args: List[str]):
    url = f'http://www.libsdl.org/release/SDL2-{version}.zip'
    path = download(url, source_dir)
    sdl2_source_dir = os.path.join(source_dir, 'sdl2')
    sdl2_build_dir = os.path.join(build_dir, 'sdl2')
    sdl2_install_dir = os.path.join(install_dir, 'sdl2')
    rm_rf(sdl2_source_dir)
    rm_rf(sdl2_build_dir)
    rm_rf(sdl2_install_dir)
    extract(path, source_dir, 'sdl2')

    mkdir_p(sdl2_build_dir)
    with cd(sdl2_build_dir):
        configuration = 'Debug' if debug else 'Release'
        cmake_args = cmake_args[:]
        cmake_args += [
            sdl2_source_dir,
            f"-DCMAKE_BUILD_TYPE={configuration}",
            f"-DCMAKE_INSTALL_PREFIX={cmake_path(sdl2_install_dir)}",
            '-DBUILD_SHARED_LIBS=OFF',
        ]
        if platform == 'windows':
            cmake_args += [
                '-G', 'Visual Studio 16 2019',
                '-DSDL_FORCE_STATIC_VCRT=ON',
                '-DHAVE_LIBC=ON',
            ]
        elif platform == 'macos':
            # システムでインストール済みかによって ON/OFF が切り替わってしまうため、
            # どの環境でも同じようにインストールされるようにするため全部 ON/OFF を明示的に指定する
            cmake_args += [
                '-DSDL_ATOMIC=OFF',
                '-DSDL_AUDIO=OFF',
                '-DSDL_VIDEO=ON',
                '-DSDL_RENDER=ON',
                '-DSDL_EVENTS=ON',
                '-DSDL_JOYSTICK=ON',
                '-DSDL_HAPTIC=ON',
                '-DSDL_POWER=ON',
                '-DSDL_THREADS=ON',
                '-DSDL_TIMERS=OFF',
                '-DSDL_FILE=OFF',
                '-DSDL_LOADSO=ON',
                '-DSDL_CPUINFO=OFF',
                '-DSDL_FILESYSTEM=OFF',
                '-DSDL_SENSOR=ON',
                '-DSDL_OPENGL=ON',
                '-DSDL_OPENGLES=ON',
                '-DSDL_RPI=OFF',
                '-DSDL_WAYLAND=OFF',
                '-DSDL_X11=OFF',
                '-DSDL_VULKAN=OFF',
                '-DSDL_VIVANTE=OFF',
                '-DSDL_COCOA=ON',
                '-DSDL_METAL=ON',
                '-DSDL_KMSDRM=OFF',
            ]
        elif platform == 'linux':
            # システムでインストール済みかによって ON/OFF が切り替わってしまうため、
            # どの環境でも同じようにインストールされるようにするため全部 ON/OFF を明示的に指定する
            cmake_args += [
                '-DSDL_ATOMIC=OFF',
                '-DSDL_AUDIO=OFF',
                '-DSDL_VIDEO=ON',
                '-DSDL_RENDER=ON',
                '-DSDL_EVENTS=ON',
                '-DSDL_JOYSTICK=ON',
                '-DSDL_HAPTIC=ON',
                '-DSDL_POWER=ON',
                '-DSDL_THREADS=ON',
                '-DSDL_TIMERS=OFF',
                '-DSDL_FILE=OFF',
                '-DSDL_LOADSO=ON',
                '-DSDL_CPUINFO=OFF',
                '-DSDL_FILESYSTEM=OFF',
                '-DSDL_SENSOR=ON',
                '-DSDL_OPENGL=ON',
                '-DSDL_OPENGLES=ON',
                '-DSDL_RPI=OFF',
                '-DSDL_WAYLAND=OFF',
                '-DSDL_X11=ON',
                '-DSDL_X11_SHARED=OFF',
                '-DSDL_X11_XCURSOR=OFF',
                '-DSDL_X11_XDBE=OFF',
                '-DSDL_X11_XFIXES=OFF',
                '-DSDL_X11_XINERAMA=OFF',
                '-DSDL_X11_XINPUT=OFF',
                '-DSDL_X11_XRANDR=OFF',
                '-DSDL_X11_XSCRNSAVER=OFF',
                '-DSDL_X11_XSHAPE=OFF',
                '-DSDL_X11_XVM=OFF',
                '-DSDL_VULKAN=OFF',
                '-DSDL_VIVANTE=OFF',
                '-DSDL_COCOA=OFF',
                '-DSDL_METAL=OFF',
                '-DSDL_KMSDRM=OFF',
            ]
        cmd(['cmake'] + cmake_args)

        cmd(['cmake', '--build', '.', '--config', configuration, f'-j{multiprocessing.cpu_count()}'])
        cmd(['cmake', '--install', '.', '--config', configuration])


@versioned
def install_sora(version, source_dir, install_dir, platform: str):
    win = platform.startswith("windows_")
    filename = f'sora-cpp-sdk-{version}_{platform}.{"zip" if win else "tar.gz"}'
    rm_rf(os.path.join(source_dir, filename))
    archive = download(
        f'https://github.com/shiguredo/sora-cpp-sdk/releases/download/{version}/{filename}',
        output_dir=source_dir)
    rm_rf(os.path.join(install_dir, 'sora'))
    extract(archive, output_dir=install_dir, output_dirname='sora')


@versioned
def install_cli11(version, install_dir):
    cli11_install_dir = os.path.join(install_dir, 'cli11')
    rm_rf(cli11_install_dir)
    cmd(['git', 'clone',
        '--branch', version,
         '--depth', '1',
         'https://github.com/CLIUtils/CLI11.git',
         cli11_install_dir])
