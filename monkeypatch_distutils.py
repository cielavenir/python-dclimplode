'''
Enhances distutils with:
    - better C++ support
    - incremental builds
    - parallel builds

 ----------------------------------------------------------------------
 Open-Source PyMOL is Copyright (C) Schrodinger, LLC.

 All Rights Reserved

 Permission to use, copy, modify, distribute, and distribute modified
 versions of this software and its built-in documentation for any
 purpose and without fee is hereby granted, provided that the above
 copyright notice appears in all copies and that both the copyright
 notice and this permission notice appear in supporting documentation,
 and that the name of Schrodinger, LLC not be used in advertising or
 publicity pertaining to distribution of the software without specific,
 written prior permission.

 SCHRODINGER, LLC DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN
 NO EVENT SHALL SCHRODINGER, LLC BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE
 USE OR PERFORMANCE OF THIS SOFTWARE.
 ----------------------------------------------------------------------
'''

import os
import shlex
import sys
import distutils.sysconfig
import distutils.ccompiler
import distutils.unixccompiler

# threaded parallel map (optional)
pmap = map

try:
    import _osx_support
    _osx_support._UNIVERSAL_CONFIG_VARS += ('CXXFLAGS', 'LDCXXSHARED',)
    _osx_support._COMPILER_CONFIG_VARS += ('LDCXXSHARED',)
except ImportError:
    _osx_support = None

distutils.unixccompiler.UnixCCompiler.executables.update({
    'compiler_cxx'      : ["c++"],
    'compiler_so_cxx'   : ["c++"],
    'linker_so_cxx'     : ["c++", "-shared"],
    'linker_exe_cxx'    : ["c++"],
})


def set_parallel_jobs(N):
    '''
    Set the number of parallel build jobs.
    N=1 : single threaded
    N=0 : use number of CPUs
    '''
    global pmap

    if N == 1:
        pmap = map
    else:
        from multiprocessing import pool
        pmap = pool.ThreadPool(N or None).map


def incremental_parallel_compile(single_compile, objects, force):
    '''
    Call `single_compile` on each item in `objects` (in parallel) if
    any dependency (source file) is newer than the object file.
    '''
    mtimes = {}

    def deps(obj):
        # parse .d file (Makefile syntax)
        with open(os.path.splitext(obj)[0] + '.d') as handle:
            contents = handle.read().split(': ', 1)[-1]
            contents = contents.replace('\\\n', ' ')
            for dep in shlex.split(contents):
                yield dep

    def need_compile(obj):
        if force:
            return True

        try:
            obj_mtime = os.path.getmtime(obj)
            for dep in deps(obj):
                if dep not in mtimes:
                    mtimes[dep] = os.path.getmtime(dep)
                if obj_mtime < mtimes[dep]:
                    return True
        except EnvironmentError:
            return True

        return False

    for _ in pmap(single_compile, filter(need_compile, objects)):
        pass


def monkeypatch(parent, name):
    '''
    Decorator to replace a function or class method. Makes the
    unpatched function available as <patchedfunction>._super
    '''
    def wrapper(func):
        orig = getattr(parent, name)
        func._super = orig
        func.__name__ = name
        setattr(parent, name, func)
        return func
    return wrapper


def strip_broken_isysroot(args, start=0):
    '''
    Strip -isysroot which don't exist from args.

    Those can come from lib/python2.7/_sysconfigdata_x86_64_apple_darwin13_4_0.py
    '''
    assert isinstance(args, list)

    try:
        i = args.index('-isysroot', start)
    except ValueError:
        return

    strip_broken_isysroot(args, i + 2)

    if not os.path.isdir(args[i + 1]):
        args[i:i + 2] = []


@monkeypatch(distutils.sysconfig, 'customize_compiler')
def customize_compiler(compiler):
    # remove problematic flags
    if sys.platform == 'linux' and (
            'icpc' in os.getenv('CXX', '') or
            'clang' in os.getenv('CC', '') or
            'clang' in os.getenv('LD', '')):
        import re
        re_flto = re.compile(r'-flto\S*|-fno-semantic-interposition')
        config_vars = distutils.sysconfig.get_config_vars()
        for (key, value) in config_vars.items():
            if re_flto.search(str(value)) is not None:
                config_vars[key] = re_flto.sub('', value)

    customize_compiler._super(compiler)

    if compiler.compiler_type != "unix":
        return

    (cxx, ccshared, ldcxxshared) = \
            distutils.sysconfig.get_config_vars('CXX', 'CCSHARED', 'LDCXXSHARED')

    cxx = os.environ.get('CXX') or cxx
    cxxflags = os.environ.get('CXXFLAGS', '') + ' ' + os.environ.get('CPPFLAGS', '')
    ldcxxshared = os.environ.get('LDCXXSHARED', ldcxxshared or '') + \
            ' ' + os.environ.get('LDFLAGS', '') + \
            ' ' + os.environ.get('CXXFLAGS', '') + \
            ' ' + os.environ.get('CPPFLAGS', '')

    cxx_cmd = cxx + ' ' + cxxflags

    # C++11 by default
    if '-std=' not in cxx_cmd:
        cxx_cmd += ' -std=c++11'

    compiler.set_executables(
            compiler_cxx=cxx_cmd,
            compiler_so_cxx=cxx_cmd + ' ' + ccshared,
            linker_so_cxx=ldcxxshared,
            linker_exe_cxx=cxx)

@monkeypatch(distutils.unixccompiler.UnixCCompiler, 'compile')
def compile(self, sources, output_dir=None, macros=None,
        include_dirs=None, debug=0, extra_preargs=None, extra_postargs=None,
        depends=None):
    '''
    Enable parallel and incremental build.

    To do a clean build, please remove the "build" directory.
    '''
    macros, objects, extra_postargs, pp_opts, build = self._setup_compile(
            output_dir, macros, include_dirs, sources, depends, extra_postargs)
    cc_args = self._get_cc_args(pp_opts, debug, extra_preargs)

    compiler_so = self.compiler_so
    compiler_so_cxx = self.compiler_so_cxx

    # strips non-existing -isysroot
    strip_broken_isysroot(compiler_so)
    strip_broken_isysroot(compiler_so_cxx)

    if sys.platform == 'darwin' and _osx_support is not None:
        # strips duplicated -isysroot
        compiler_so = _osx_support.compiler_fixup(compiler_so, cc_args + extra_postargs)
        compiler_so_cxx = _osx_support.compiler_fixup(compiler_so_cxx, cc_args + extra_postargs)

    # generate dependency (.d) file
    cc_args.append('-MMD')

    def _single_compile(obj):
        src = build[obj][0]

        # _compile
        compiler = compiler_so_cxx \
                if self.detect_language(src) == 'c++' \
                else compiler_so
        try:
            self.spawn(compiler + cc_args + [src, '-o', obj] + extra_postargs)
        except distutils.errors.DistutilsExecError as msg:
            raise distutils.errors.CompileError(msg)

    incremental_parallel_compile(_single_compile, objects, self.force)

    return objects

# Use setuptools' standard MSVC compiler on Windows. The former override relied
# on private compiler attributes, including dry_run removed in setuptools 81.
