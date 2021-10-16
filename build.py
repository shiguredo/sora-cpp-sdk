import subprocess
import logging
import os
import urllib.parse
import zipfile
import tarfile
import shutil
import platform
import argparse
import multiprocessing
from typing import Callable, Optional, List, Union, Dict
if platform.system() == 'Windows':
    import winreg


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
            dir = name
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
def extract(file: str, output_dir: str, output_dirname: str):
    path = os.path.join(output_dir, output_dirname)
    logging.info(f"Extract {file} to {path}")
    if file.endswith('.tar.gz'):
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
    elif file.endswith('.zip'):
        rm_rf(path)
        with zipfile.ZipFile(file) as z:
            dir = is_single_dir_zip(z)
            if dir is None:
                os.makedirs(path, exist_ok=True)
                z.extractall(path)
            else:
                logging.info(f"Directory {dir} is stripped")
                path2 = os.path.join(output_dir, dir)
                rm_rf(path2)
                z.extractall(output_dir)
                if path != path2:
                    logging.debug(f"mv {path2} {path}")
                    os.replace(path2, path)
    else:
        raise Exception('file should end with .tar.gz or .zip')


@versioned
def install_webrtc(version, source_dir, install_dir, platform):
    win = platform == "windows"
    filename = f'webrtc.{platform}.{"zip" if win else "tar.gz"}'
    archive = download(
        f'https://github.com/shiguredo-webrtc-build/webrtc-build/releases/download/{version}/{filename}',
        output_dir=source_dir)
    extract(archive, output_dir=install_dir, output_dirname='webrtc')


def build_install_webrtc(version, source_dir, build_dir, install_dir, platform):
    source_dir = os.path.join(source_dir, 'webrtc')
    build_dir = os.path.join(build_dir, 'webrtc')
    install_dir = os.path.join(install_dir, 'webrtc')
    mkdir_p(source_dir)
    with cd(source_dir):
        cmd(['git', 'clone', 'https://github.com/shiguredo-webrtc-build/webrtc-build.git'])
        # version_file = read_version_file(os.path.join('webrtc-build', 'VERSIONS'))


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
        cmd(['git', 'clone', tools_url, 'tools'])
        with cd('tools'):
            cmd(['git', 'reset', '--hard', tools_commit])
            cmd(['python',
                os.path.join('clang', 'scripts', 'update.py'),
                '--output-dir', os.path.join(llvm_dir, 'clang')])

        # 特定バージョンの libcxx を利用する
        cmd(['git', 'clone', libcxx_url, 'libcxx'])
        with cd('libcxx'):
            cmd(['git', 'reset', '--hard', libcxx_commit])

        # __config_site のために特定バージョンの buildtools を取得する
        cmd(['git', 'clone', buildtools_url, 'buildtools'])
        with cd('buildtools'):
            cmd(['git', 'reset', '--hard', buildtools_commit])
        shutil.copyfile(os.path.join(llvm_dir, 'buildtools', 'third_party', 'libc++', '__config_site'),
                        os.path.join(llvm_dir, 'libcxx', 'include', '__config_site'))


@versioned
def install_boost(
        version: str, source_dir, build_dir, install_dir,
        cxx: str, cxxflags: List[str], toolset, visibility, target_os):
    version_underscore = version.replace('.', '_')
    archive = download(
        f'https://boostorg.jfrog.io/artifactory/main/release/{version}/source/boost_{version_underscore}.tar.gz',
        source_dir)
    extract(archive, output_dir=build_dir, output_dirname='boost')
    with cd(os.path.join(build_dir, 'boost')):
        bootstrap = '.\\bootstrap.bat' if target_os == 'windows' else './bootstrap.sh'
        b2 = '.\\b2' if target_os == 'windows' else './b2'

        cmd([bootstrap])
        if len(cxx) != 0:
            with open('project-config.jam', 'w') as f:
                f.write(f'using {toolset} : : {cxx} : ;')
        cmd([
            b2,
            'install',
            f'--prefix={os.path.join(install_dir, "boost")}',
            '--with-filesystem',
            '--with-json',
            '--with-date_time',
            '--with-regex',
            '--with-system',
            '--layout=system',
            '--ignore-site-config',
            'variant=release',
            f'cxxflags={" ".join(cxxflags)}',
            f'toolset={toolset}',
            f'visibility={visibility}',
            f'target-os={target_os}',
            'address-model=64',
            'link=static',
            'runtime-link=static',
            'threading=multi'])

#  && /root/setup_boost.sh "$BOOST_VERSION" /root/boost-source /root/_cache/boost \
#  && cd /root/boost-source/source \
#  && echo 'using clang : : /root/llvm/clang/bin/clang++ : ;' > project-config.jam \
#  && ./b2 \
#    cxxflags=' \
#      -D_LIBCPP_ABI_UNSTABLE \
#      -D_LIBCPP_DISABLE_AVAILABILITY \
#      -nostdinc++ \
#      -isystem/root/llvm/libcxx/include \
#    ' \
#    linkflags=' \
#    ' \
#    toolset=clang \
#    visibility=global \
#    target-os=linux \
#    address-model=64 \
#    link=static \
#    variant=release \
#    install \
#    -j`nproc` \
#    --ignore-site-config \
#    --prefix=/root/boost \
#    --with-filesystem \
#    --with-json


def cmake_path(path: str) -> str:
    return path.replace('\\', '/')


@versioned
def install_rotor(version, source_dir, build_dir, install_dir, boost_root, cmake_args: List[str]):
    source_dir = os.path.join(source_dir, 'rotor')
    build_dir = os.path.join(build_dir, 'rotor')
    install_dir = os.path.join(install_dir, 'rotor')
    rm_rf(source_dir)
    rm_rf(build_dir)
    rm_rf(install_dir)
    # cmd(['git', 'clone', 'https://github.com/basiliscos/cpp-rotor.git', source_dir])
    cmd(['git', 'clone', 'https://github.com/melpon/cpp-rotor.git', source_dir])
    with cd(source_dir):
        cmd(['git', 'checkout', version])
    mkdir_p(build_dir)
    with cd(build_dir):
        cmd(['cmake', source_dir,
             '-DBUILD_BOOST_ASIO=ON',
             '-DBoost_USE_STATIC_RUNTIME=ON',
             f'-DCMAKE_INSTALL_PREFIX={cmake_path(install_dir)}',
             '-DCMAKE_BUILD_TYPE=Release',
             f'-DBOOST_ROOT={cmake_path(boost_root)}'] + cmake_args)
        cmd(['cmake', '--build', '.', f'-j{multiprocessing.cpu_count()}', '--config', 'Release'])
        cmd(['cmake', '--install', '.'])


@versioned
def install_cmake(version, source_dir, install_dir, platform: str, ext):
    url = f'https://github.com/Kitware/CMake/releases/download/v{version}/cmake-{version}-{platform}.{ext}'
    path = download(url, source_dir)
    extract(path, install_dir, 'cmake')


class PlatformTarget(object):
    def __init__(self, os, osver, arch):
        self.os = os
        self.osver = osver
        self.arch = arch

    @property
    def package_name(self):
        if self.os == 'windows':
            return f'windows-{self.osver}'
        if self.os == 'macos':
            return f'macos-{self.osver}'
        if self.os == 'ubuntu':
            return f'ubuntu-{self.osver}_{self.arch}'
        if self.os == 'ios':
            return 'ios'
        if self.os == 'android':
            return 'android'
        if self.os == 'raspberry-pi-os':
            return f'raspberry-pi-os_{self.arch}'
        if self.os == 'jetson':
            return f'ubuntu-18.04_armv8_jetson_{self.osver}'
        raise Exception('error')


def get_windows_osver():
    osver = platform.release()
    with winreg.OpenKeyEx(winreg.HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion") as key:
        return osver + '.' + winreg.QueryValueEx(key, "ReleaseId")[0]


def get_macos_osver():
    platform.mac_ver()[0]


def get_build_platform() -> PlatformTarget:
    os = platform.system()
    if os == 'Windows':
        os = 'windows'
        osver = get_windows_osver()
    elif os == 'Darwin':
        os = 'macos'
        osver = get_macos_osver()
    elif os == 'Linux':
        release = read_version_file('/etc/os-release')
        os = release['NAME']
        if os == 'Ubuntu':
            os = 'ubuntu'
            osver = release['VERSION_ID']
        else:
            raise Exception(f'OS {os} not supported')
        pass
    else:
        raise Exception(f'OS {os} not supported')

    arch = platform.machine()
    if arch in ('AMD64', 'x86_64'):
        arch = 'x86_64'
    elif arch in ('aarch64', 'arm64'):
        arch = 'arm64'
    else:
        raise Exception(f'Arch {arch} not supported')

    return PlatformTarget(os, osver, arch)


SUPPORTED_BUILD_OS = [
    'windows',
    'macos',
    'ubuntu',
]
SUPPORTED_TARGET_OS = SUPPORTED_BUILD_OS + [
    'ios',
    'android',
    'raspberry-pi-os',
    'jetson'
]


class Platform(object):
    def _check(self, flag):
        if not flag:
            raise Exception('Not supported')

    def _check_platform_target(self, p: PlatformTarget):
        if p.os == 'raspberry-pi-os':
            self._check(p.arch in ('armv6', 'armv7', 'armv8'))
        elif p.os == 'jetson':
            self._check(p.osver in ('nano', 'xavier'))
            self._check(p.arch == 'arm64')
        else:
            self._check(p.arch in ('x86_64', 'arm64'))

    def __init__(self, target_os, target_osver, target_arch):
        build = get_build_platform()
        target = PlatformTarget(target_os, target_osver, target_arch)

        self._check(build.os in SUPPORTED_BUILD_OS)
        self._check(target.os in SUPPORTED_TARGET_OS)

        self._check_platform_target(build)
        self._check_platform_target(target)

        if target.os == 'windows':
            self._check(target.arch == 'x86_64')
            self._check(build.os == 'windows')
            self._check(build.arch == 'x86_64')
        if target.os == 'macos':
            self._check(build.os == 'macos')
            self._check(build.arch == 'x86_64')
        if target.os == 'ios':
            self._check(build.os == 'macos')
            self._check(build.arch == 'x86_64')
        if target.os == 'android':
            self._check(build.os == 'ubuntu')
            self._check(build.arch == 'x86_64')
        if target.os == 'ubuntu':
            self._check(build.os == 'ubuntu')
            self._check(build.arch == 'x86_64')
            self._check(build.osver == target.osver)
        if target.os == 'raspberry-pi-os':
            self._check(build.os == 'ubuntu')
            self._check(build.arch == 'x86_64')
        if target.os == 'jetson':
            self._check(build.os == 'ubuntu')
            self._check(build.arch == 'x86_64')

        self.build = build
        self.target = target


def read_webrtc_version_file(webrtc_dir: str) -> Dict[str, str]:
    # プラットフォームによって VERSIONS ファイルの位置が違うので複数の場所を探す
    path = os.path.join(webrtc_dir, 'VERSIONS')
    if os.path.exists(path):
        return read_version_file(path)
    path = os.path.join(webrtc_dir, 'release', 'VERSIONS')
    if os.path.exists(path):
        return read_version_file(path)
    raise FileNotFoundError()


BASE_DIR = os.path.abspath(os.path.dirname(__file__))


def install_deps(platform, source_dir, build_dir, install_dir, webrtc_source_build: bool):
    with cd(BASE_DIR):
        version = read_version_file('VERSION')

        if webrtc_source_build:
            install_webrtc_args = {
                'version': version['WEBRTC_BUILD_VERSION'],
                'source_dir': source_dir,
                'build_dir': build_dir,
                'install_dir': install_dir,
                'platform': '',
            }
            if platform.target.os == 'windows':
                install_webrtc_args['platform'] = 'windows'
            if platform.target.os == 'ubuntu':
                install_webrtc_args['platform'] = f'ubuntu-{platform.target.osver}_{platform.target.arch}'

            build_install_webrtc(**install_webrtc_args)
        else:
            install_webrtc_args = {
                'version': version['WEBRTC_BUILD_VERSION'],
                'version_file': os.path.join(install_dir, 'webrtc.version'),
                'source_dir': source_dir,
                'install_dir': install_dir,
                'platform': '',
            }
            if platform.target.os == 'windows':
                install_webrtc_args['platform'] = 'windows'
            if platform.target.os == 'macos':
                install_webrtc_args['platform'] = f'macos_{platform.target.arch}'
            if platform.target.os == 'ios':
                install_webrtc_args['platform'] = 'ios'
            if platform.target.os == 'android':
                install_webrtc_args['platform'] = 'android'
            if platform.target.os == 'ubuntu':
                install_webrtc_args['platform'] = f'ubuntu-{platform.target.osver}_{platform.target.arch}'
            if platform.target.os == 'raspberry-pi-os':
                install_webrtc_args['platform'] = f'raspberry-pi-os_{platform.target.arch}'
            if platform.target.os == 'jetson':
                install_webrtc_args['platform'] = 'ubuntu-18.04_armv8'

            install_webrtc(**install_webrtc_args)

        webrtc_version = read_webrtc_version_file(os.path.join(install_dir, 'webrtc'))

        # Windows は MSVC を使うので LLVM は不要
        if platform.target.os != 'windows':
            tools_url = webrtc_version['WEBRTC_SRC_TOOLS_URL']
            tools_commit = webrtc_version['WEBRTC_SRC_TOOLS_COMMIT']
            libcxx_url = webrtc_version['WEBRTC_SRC_BUILDTOOLS_THIRD_PARTY_LIBCXX_TRUNK_URL']
            libcxx_commit = webrtc_version['WEBRTC_SRC_BUILDTOOLS_THIRD_PARTY_LIBCXX_TRUNK_COMMIT']
            buildtools_url = webrtc_version['WEBRTC_SRC_BUILDTOOLS_URL']
            buildtools_commit = webrtc_version['WEBRTC_SRC_BUILDTOOLS_COMMIT']
            install_llvm_args = {
                'version':
                    f'{tools_url}.{tools_commit}.'
                    f'{libcxx_url}.{libcxx_commit}.'
                    f'{buildtools_url}.{buildtools_commit}',
                'version_file': os.path.join(install_dir, 'llvm.version'),
                'install_dir': install_dir,
                'tools_url': tools_url,
                'tools_commit': tools_commit,
                'libcxx_url': libcxx_url,
                'libcxx_commit': libcxx_commit,
                'buildtools_url': buildtools_url,
                'buildtools_commit': buildtools_commit,
            }
            install_llvm(**install_llvm_args)

        install_boost_args = {
            'version': version['BOOST_VERSION'],
            'version_file': os.path.join(install_dir, 'boost.version'),
            'source_dir': source_dir,
            'build_dir': build_dir,
            'install_dir': install_dir,
            'cxx': '',
            'cxxflags': [],
            'toolset': 'msvc',
            'visibility': 'global',
            'target_os': 'windows',
        }
        if platform.target.os != 'windows':
            install_boost_args['cxx'] = os.path.join(install_dir, 'llvm', 'clang', 'bin', 'clang++')
            install_boost_args['cxxflags'] = [
                '-D_LIBCPP_ABI_UNSTABLE',
                '-D_LIBCPP_DISABLE_AVAILABILITY',
                '-nostdinc++',
                f"-isystem{os.path.join(install_dir, 'llvm', 'libcxx', 'include')}",
            ]
            install_boost_args['toolset'] = 'clang'
            install_boost_args['visibility'] = 'global'
            if platform.target.os == 'macos':
                install_boost_args['target_os'] = 'darwin'
            if platform.target.os == 'ios':
                install_boost_args['target_os'] = 'iphone'
            else:
                install_boost_args['target_os'] = 'linux'
        install_boost(**install_boost_args)

        install_cmake_args = {
            'version': version['CMAKE_VERSION'],
            'version_file': os.path.join(install_dir, 'cmake.version'),
            'source_dir': source_dir,
            'install_dir': install_dir,
            'platform': '',
            'ext': 'tar.gz'
        }
        if platform.build.os == 'windows' and platform.build.arch == 'x86_64':
            install_cmake_args['platform'] = 'windows-x86_64'
            install_cmake_args['ext'] = 'zip'
        elif platform.build.os == 'macos':
            install_cmake_args['platform'] = 'macos-universal'
        elif platform.build.os == 'ubuntu' and platform.build.arch == 'x86_64':
            install_cmake_args['platform'] = 'linux-x86_64'
        elif platform.build.os == 'ubuntu' and platform.build.arch == 'arm64':
            install_cmake_args['platform'] = 'linux-aarch64'
        else:
            raise Exception('Failed to install CMake')
        install_cmake(**install_cmake_args)

        add_path(os.path.join(install_dir, 'cmake', 'bin'))

        libcxx_include_dir = cmake_path(os.path.join(install_dir, 'llvm', 'libcxx', 'include'))
        cmake_c_compiler = cmake_path(os.path.join(install_dir, 'llvm', 'clang', 'bin', 'clang'))
        cmake_cxx_compiler = cmake_path(os.path.join(install_dir, 'llvm', 'clang', 'bin', 'clang++'))

        install_rotor_args = {
            'version': version['ROTOR_VERSION'],
            'version_file': os.path.join(install_dir, 'rotor.version'),
            'source_dir': source_dir,
            'build_dir': build_dir,
            'install_dir': install_dir,
            'boost_root': os.path.join(install_dir, 'boost'),
            'cmake_args': []
        }
        if platform.build.os != 'windows':
            install_rotor_args['cmake_args'] = [
                f'-DCMAKE_C_COMPILER={cmake_c_compiler}',
                f'-DCMAKE_CXX_COMPILER={cmake_cxx_compiler}',
                f"-DCMAKE_CXX_FLAGS=-D_LIBCPP_ABI_UNSTABLE -D_LIBCPP_DISABLE_AVAILABILITY \
                    -nostdinc++ -isystem{libcxx_include_dir}",
            ]
        install_rotor(**install_rotor_args)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("target", choices=['windows', 'macos_x86_64', 'macos_arm64', 'ubuntu-20.04_x86_64'])
    parser.add_argument("--webrtc-source-build", action='store_true')
    args = parser.parse_args()
    if args.target == 'windows':
        platform = Platform('windows', get_windows_osver(), 'x86_64')
    elif args.target == 'macos_x86_64':
        platform = Platform('macos', get_macos_osver(), 'x86_64')
    elif args.target == 'macos_arm64':
        platform = Platform('macos', get_macos_osver(), 'arm64')
    elif args.target == 'ubuntu-20.04_x86_64':
        platform = Platform('ubuntu', '20.04', 'x86_64')
    else:
        raise Exception(f'Unknown target {args.target}')

    logging.info(f'Build platform: {platform.build.package_name}')
    logging.info(f'Target platform: {platform.target.package_name}')

    dir = platform.target.package_name
    if args.webrtc_source_build:
        dir += ".webrtc_source_build"
    source_dir = os.path.join(BASE_DIR, '_source', dir)
    build_dir = os.path.join(BASE_DIR, '_build', dir)
    install_dir = os.path.join(BASE_DIR, '_install', dir)
    mkdir_p(source_dir)
    mkdir_p(build_dir)
    mkdir_p(install_dir)

    install_deps(platform, source_dir, build_dir, install_dir, args.webrtc_source_build)

    sora_build_dir = os.path.join(build_dir, 'sora')
    mkdir_p(sora_build_dir)
    with cd(sora_build_dir):
        cmake_args = []
        cmake_args.append('-DCMAKE_BUILD_TYPE=Release')
        cmake_args.append(f"-DBOOST_ROOT={cmake_path(os.path.join(install_dir, 'boost'))}")
        cmake_args.append(f"-DROTOR_ROOT_DIR={cmake_path(os.path.join(install_dir, 'rotor'))}")
        cmake_args.append(f"-DWEBRTC_INCLUDE_DIR={cmake_path(os.path.join(install_dir, 'webrtc', 'include'))}")
        if platform.build.os == 'windows':
            cmake_args.append(f"-DWEBRTC_LIBRARY_DIR={cmake_path(os.path.join(install_dir, 'webrtc', 'release'))}")
        if platform.build.os == 'ubuntu':
            cmake_args.append(f"-DWEBRTC_LIBRARY_DIR={cmake_path(os.path.join(install_dir, 'webrtc', 'lib'))}")
            cmake_args.append(
                f"-DCMAKE_C_COMPILER={cmake_path(os.path.join(install_dir, 'llvm', 'clang', 'bin', 'clang'))}")
            cmake_args.append(
                f"-DCMAKE_CXX_COMPILER={cmake_path(os.path.join(install_dir, 'llvm', 'clang', 'bin', 'clang++'))}")
            cmake_args.append("-DUSE_LIBCXX=ON'")
            cmake_args.append(
                f"-DLIBCXX_INCLUDE_DIR={cmake_path(os.path.join(install_dir, 'llvm', 'libcxx', 'include'))}")

        cmd(['cmake', BASE_DIR] + cmake_args)
        cmd(['cmake', '--build', '.', f'-j{multiprocessing.cpu_count()}', '--config', 'Debug'])
        cmd(['ctest', '--verbose'])


if __name__ == '__main__':
    main()
