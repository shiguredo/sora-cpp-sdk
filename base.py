import subprocess
import logging
import os
import urllib.parse
import zipfile
import tarfile
import shutil
import platform
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
            libcxx_dir=os.path.join(webrtc_source_dir, 'src', 'buildtools', 'third_party', 'libc++', 'trunk'),)
    else:
        return WebrtcInfo(
            version_file=os.path.join(webrtc_install_dir, 'VERSIONS'),
            webrtc_include_dir=os.path.join(webrtc_install_dir, 'include'),
            webrtc_library_dir=os.path.join(install_dir, 'webrtc', 'lib'),
            clang_dir=os.path.join(install_dir, 'llvm', 'clang'),
            libcxx_dir=os.path.join(install_dir, 'llvm', 'libcxx'),
        )


@versioned
def install_boost(
        version: str, source_dir, build_dir, install_dir,
        debug: bool, cxx: str, cflags: List[str], cxxflags: List[str], linkflags: List[str],
        toolset, visibility, target_os, architecture,
        android_ndk, native_api_level):
    version_underscore = version.replace('.', '_')
    archive = download(
        f'https://boostorg.jfrog.io/artifactory/main/release/{version}/source/boost_{version_underscore}.tar.gz',
        source_dir)
    extract(archive, output_dir=build_dir, output_dirname='boost')
    with cd(os.path.join(build_dir, 'boost')):
        bootstrap = '.\\bootstrap.bat' if target_os == 'windows' else './bootstrap.sh'
        b2 = 'b2' if target_os == 'windows' else './b2'
        runtime_link = 'static' if target_os == 'windows' else 'shared'

        cmd([bootstrap])

        if target_os == 'iphone':
            # iOS の場合、シミュレータとデバイス用のライブラリを作って
            # lipo で結合する
            IOS_BUILD_TARGETS = [('x86_64', 'iphonesimulator'), ('arm64', 'iphoneos')]
            for arch, sdk in IOS_BUILD_TARGETS:
                clangpp = cmdcap(['xcodebuild', '-find', 'clang++'])
                sysroot = cmdcap(['xcrun', '--sdk', sdk, '--show-sdk-path'])
                boost_arch = 'x86' if arch == 'x86_64' else 'arm'
                with open('project-config.jam', 'w') as f:
                    f.write(f"using clang \
                        : iphone \
                        : {clangpp} -arch {arch} -isysroot {sysroot} \
                          -fembed-bitcode \
                          -mios-version-min=10.0 \
                          -fvisibility=hidden \
                        : <striper> <root>{sysroot} \
                        ; \
                        ")
                cmd([
                    b2,
                    'install',
                    '-d+0',
                    f'--build-dir={os.path.join(build_dir, "boost", f"build-{arch}-{sdk}")}',
                    f'--prefix={os.path.join(build_dir, "boost", f"install-{arch}-{sdk}")}',
                    '--with-json',
                    '--layout=system',
                    '--ignore-site-config',
                    f'variant={"debug" if debug else "release"}',
                    f'cflags={" ".join(cflags)}',
                    f'cxxflags={" ".join(cxxflags)}',
                    f'linkflags={" ".join(linkflags)}',
                    f'toolset={toolset}',
                    f'visibility={visibility}',
                    f'target-os={target_os}',
                    'address-model=64',
                    'link=static',
                    f'runtime-link={runtime_link}',
                    'threading=multi',
                    f'architecture={boost_arch}'])
            arch, sdk = IOS_BUILD_TARGETS[0]
            installed_path = os.path.join(build_dir, 'boost', f'install-{arch}-{sdk}')
            rm_rf(os.path.join(install_dir, 'boost'))
            cmd(['cp', '-r', installed_path, os.path.join(install_dir, 'boost')])

            for lib in enum_all_files(os.path.join(installed_path, 'lib'), os.path.join(installed_path, 'lib')):
                if not lib.endswith('.a'):
                    continue
                files = [os.path.join(build_dir, 'boost', f'install-{arch}-{sdk}', 'lib', lib)
                         for arch, sdk in IOS_BUILD_TARGETS]
                cmd(['lipo', '-create', '-output', os.path.join(install_dir, 'boost', 'lib', lib)] + files)
        elif target_os == 'android':
            # Android の場合、android-ndk を使ってビルドする
            with open('project-config.jam', 'w') as f:
                bin = os.path.join(android_ndk, 'toolchains', 'llvm', 'prebuilt', 'linux-x86_64', 'bin')
                sysroot = os.path.join(android_ndk, 'toolchains', 'llvm', 'prebuilt', 'linux-x86_64', 'sysroot')
                f.write(f"using clang \
                    : android \
                    : {os.path.join(bin, f'aarch64-linux-android{native_api_level}-clang++')} \
                      --sysroot={sysroot} \
                    : <archiver>{os.path.join(bin, 'llvm-ar')} \
                      <ranlib>{os.path.join(bin, 'llvm-ranlib')} \
                    ; \
                    ")
            cmd([
                b2,
                'install',
                '-d+0',
                f'--prefix={os.path.join(install_dir, "boost")}',
                '--with-json',
                '--layout=system',
                '--ignore-site-config',
                f'variant={"debug" if debug else "release"}',
                f'compileflags=--sysroot={sysroot}',
                f'cflags={" ".join(cflags)}',
                f'cxxflags={" ".join(cxxflags)}',
                f'linkflags={" ".join(linkflags)}',
                f'toolset={toolset}',
                f'visibility={visibility}',
                f'target-os={target_os}',
                'address-model=64',
                'link=static',
                f'runtime-link={runtime_link}',
                'threading=multi',
                'architecture=arm'])
        else:
            if len(cxx) != 0:
                with open('project-config.jam', 'w') as f:
                    f.write(f'using {toolset} : : {cxx} : ;')
            cmd([
                b2,
                'install',
                '-d+0',
                f'--prefix={os.path.join(install_dir, "boost")}',
                '--with-json',
                '--layout=system',
                '--ignore-site-config',
                f'variant={"debug" if debug else "release"}',
                f'cflags={" ".join(cflags)}',
                f'cxxflags={" ".join(cxxflags)}',
                f'linkflags={" ".join(linkflags)}',
                f'toolset={toolset}',
                f'visibility={visibility}',
                f'target-os={target_os}',
                'address-model=64',
                'link=static',
                f'runtime-link={runtime_link}',
                'threading=multi',
                f'architecture={architecture}'])


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
def install_sdl2(version, source_dir, build_dir, install_dir, debug: bool):
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
        cmd(['cmake',
            '-G', 'Visual Studio 16 2019',
             f"-DCMAKE_BUILD_TYPE={configuration}",
             '-DSDL_FORCE_STATIC_VCRT=ON',
             '-DBUILD_SHARED_LIBS=OFF',
             '-DHAVE_LIBC=ON',
             f"-DCMAKE_INSTALL_PREFIX={cmake_path(sdl2_install_dir)}",
             sdl2_source_dir])

        cmd(['cmake', '--build', '.', '--config', configuration])
        cmd(['cmake', '--build', '.', '--config', configuration, '--target', 'INSTALL'])


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
