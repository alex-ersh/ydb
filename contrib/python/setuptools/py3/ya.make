# Generated by devtools/yamaker (pypi).

PY3_LIBRARY()

VERSION(69.2.0)

LICENSE(MIT)

PEERDIR(
    library/python/resource
)

NO_LINT()

NO_CHECK_IMPORTS(
    _distutils_hack.*
    pkg_resources._vendor.*
    setuptools.*
)

PY_SRCS(
    TOP_LEVEL
    _distutils_hack/__init__.py
    _distutils_hack/override.py
    pkg_resources/__init__.py
    pkg_resources/_vendor/__init__.py
    pkg_resources/_vendor/importlib_resources/__init__.py
    pkg_resources/_vendor/importlib_resources/_adapters.py
    pkg_resources/_vendor/importlib_resources/_common.py
    pkg_resources/_vendor/importlib_resources/_compat.py
    pkg_resources/_vendor/importlib_resources/_itertools.py
    pkg_resources/_vendor/importlib_resources/_legacy.py
    pkg_resources/_vendor/importlib_resources/abc.py
    pkg_resources/_vendor/importlib_resources/readers.py
    pkg_resources/_vendor/importlib_resources/simple.py
    pkg_resources/_vendor/jaraco/__init__.py
    pkg_resources/_vendor/jaraco/context.py
    pkg_resources/_vendor/jaraco/functools.py
    pkg_resources/_vendor/jaraco/text/__init__.py
    pkg_resources/_vendor/more_itertools/__init__.py
    pkg_resources/_vendor/more_itertools/__init__.pyi
    pkg_resources/_vendor/more_itertools/more.py
    pkg_resources/_vendor/more_itertools/more.pyi
    pkg_resources/_vendor/more_itertools/recipes.py
    pkg_resources/_vendor/more_itertools/recipes.pyi
    pkg_resources/_vendor/packaging/__init__.py
    pkg_resources/_vendor/packaging/_elffile.py
    pkg_resources/_vendor/packaging/_manylinux.py
    pkg_resources/_vendor/packaging/_musllinux.py
    pkg_resources/_vendor/packaging/_parser.py
    pkg_resources/_vendor/packaging/_structures.py
    pkg_resources/_vendor/packaging/_tokenizer.py
    pkg_resources/_vendor/packaging/markers.py
    pkg_resources/_vendor/packaging/metadata.py
    pkg_resources/_vendor/packaging/requirements.py
    pkg_resources/_vendor/packaging/specifiers.py
    pkg_resources/_vendor/packaging/tags.py
    pkg_resources/_vendor/packaging/utils.py
    pkg_resources/_vendor/packaging/version.py
    pkg_resources/_vendor/platformdirs/__init__.py
    pkg_resources/_vendor/platformdirs/__main__.py
    pkg_resources/_vendor/platformdirs/android.py
    pkg_resources/_vendor/platformdirs/api.py
    pkg_resources/_vendor/platformdirs/macos.py
    pkg_resources/_vendor/platformdirs/unix.py
    pkg_resources/_vendor/platformdirs/version.py
    pkg_resources/_vendor/platformdirs/windows.py
    pkg_resources/_vendor/typing_extensions.py
    pkg_resources/_vendor/zipp.py
    pkg_resources/extern/__init__.py
    setuptools/__init__.py
    setuptools/_core_metadata.py
    setuptools/_distutils/__init__.py
    setuptools/_distutils/_collections.py
    setuptools/_distutils/_functools.py
    setuptools/_distutils/_log.py
    setuptools/_distutils/_macos_compat.py
    setuptools/_distutils/_modified.py
    setuptools/_distutils/_msvccompiler.py
    setuptools/_distutils/archive_util.py
    setuptools/_distutils/bcppcompiler.py
    setuptools/_distutils/ccompiler.py
    setuptools/_distutils/cmd.py
    setuptools/_distutils/command/__init__.py
    setuptools/_distutils/command/_framework_compat.py
    setuptools/_distutils/command/bdist.py
    setuptools/_distutils/command/bdist_dumb.py
    setuptools/_distutils/command/bdist_rpm.py
    setuptools/_distutils/command/build.py
    setuptools/_distutils/command/build_clib.py
    setuptools/_distutils/command/build_ext.py
    setuptools/_distutils/command/build_py.py
    setuptools/_distutils/command/build_scripts.py
    setuptools/_distutils/command/check.py
    setuptools/_distutils/command/clean.py
    setuptools/_distutils/command/config.py
    setuptools/_distutils/command/install.py
    setuptools/_distutils/command/install_data.py
    setuptools/_distutils/command/install_egg_info.py
    setuptools/_distutils/command/install_headers.py
    setuptools/_distutils/command/install_lib.py
    setuptools/_distutils/command/install_scripts.py
    setuptools/_distutils/command/py37compat.py
    setuptools/_distutils/command/register.py
    setuptools/_distutils/command/sdist.py
    setuptools/_distutils/command/upload.py
    setuptools/_distutils/config.py
    setuptools/_distutils/core.py
    setuptools/_distutils/cygwinccompiler.py
    setuptools/_distutils/debug.py
    setuptools/_distutils/dep_util.py
    setuptools/_distutils/dir_util.py
    setuptools/_distutils/dist.py
    setuptools/_distutils/errors.py
    setuptools/_distutils/extension.py
    setuptools/_distutils/fancy_getopt.py
    setuptools/_distutils/file_util.py
    setuptools/_distutils/filelist.py
    setuptools/_distutils/log.py
    setuptools/_distutils/msvc9compiler.py
    setuptools/_distutils/msvccompiler.py
    setuptools/_distutils/py38compat.py
    setuptools/_distutils/py39compat.py
    setuptools/_distutils/spawn.py
    setuptools/_distutils/sysconfig.py
    setuptools/_distutils/text_file.py
    setuptools/_distutils/unixccompiler.py
    setuptools/_distutils/util.py
    setuptools/_distutils/version.py
    setuptools/_distutils/versionpredicate.py
    setuptools/_entry_points.py
    setuptools/_imp.py
    setuptools/_importlib.py
    setuptools/_itertools.py
    setuptools/_normalization.py
    setuptools/_path.py
    setuptools/_reqs.py
    setuptools/_vendor/__init__.py
    setuptools/_vendor/importlib_metadata/__init__.py
    setuptools/_vendor/importlib_metadata/_adapters.py
    setuptools/_vendor/importlib_metadata/_collections.py
    setuptools/_vendor/importlib_metadata/_compat.py
    setuptools/_vendor/importlib_metadata/_functools.py
    setuptools/_vendor/importlib_metadata/_itertools.py
    setuptools/_vendor/importlib_metadata/_meta.py
    setuptools/_vendor/importlib_metadata/_py39compat.py
    setuptools/_vendor/importlib_metadata/_text.py
    setuptools/_vendor/importlib_resources/__init__.py
    setuptools/_vendor/importlib_resources/_adapters.py
    setuptools/_vendor/importlib_resources/_common.py
    setuptools/_vendor/importlib_resources/_compat.py
    setuptools/_vendor/importlib_resources/_itertools.py
    setuptools/_vendor/importlib_resources/_legacy.py
    setuptools/_vendor/importlib_resources/abc.py
    setuptools/_vendor/importlib_resources/readers.py
    setuptools/_vendor/importlib_resources/simple.py
    setuptools/_vendor/jaraco/__init__.py
    setuptools/_vendor/jaraco/context.py
    setuptools/_vendor/jaraco/functools.py
    setuptools/_vendor/jaraco/text/__init__.py
    setuptools/_vendor/more_itertools/__init__.py
    setuptools/_vendor/more_itertools/__init__.pyi
    setuptools/_vendor/more_itertools/more.py
    setuptools/_vendor/more_itertools/more.pyi
    setuptools/_vendor/more_itertools/recipes.py
    setuptools/_vendor/more_itertools/recipes.pyi
    setuptools/_vendor/ordered_set.py
    setuptools/_vendor/packaging/__init__.py
    setuptools/_vendor/packaging/_elffile.py
    setuptools/_vendor/packaging/_manylinux.py
    setuptools/_vendor/packaging/_musllinux.py
    setuptools/_vendor/packaging/_parser.py
    setuptools/_vendor/packaging/_structures.py
    setuptools/_vendor/packaging/_tokenizer.py
    setuptools/_vendor/packaging/markers.py
    setuptools/_vendor/packaging/metadata.py
    setuptools/_vendor/packaging/requirements.py
    setuptools/_vendor/packaging/specifiers.py
    setuptools/_vendor/packaging/tags.py
    setuptools/_vendor/packaging/utils.py
    setuptools/_vendor/packaging/version.py
    setuptools/_vendor/tomli/__init__.py
    setuptools/_vendor/tomli/_parser.py
    setuptools/_vendor/tomli/_re.py
    setuptools/_vendor/tomli/_types.py
    setuptools/_vendor/typing_extensions.py
    setuptools/_vendor/zipp.py
    setuptools/archive_util.py
    setuptools/build_meta.py
    setuptools/command/__init__.py
    setuptools/command/_requirestxt.py
    setuptools/command/alias.py
    setuptools/command/bdist_egg.py
    setuptools/command/bdist_rpm.py
    setuptools/command/build.py
    setuptools/command/build_clib.py
    setuptools/command/build_ext.py
    setuptools/command/build_py.py
    setuptools/command/develop.py
    setuptools/command/dist_info.py
    setuptools/command/easy_install.py
    setuptools/command/editable_wheel.py
    setuptools/command/egg_info.py
    setuptools/command/install.py
    setuptools/command/install_egg_info.py
    setuptools/command/install_lib.py
    setuptools/command/install_scripts.py
    setuptools/command/register.py
    setuptools/command/rotate.py
    setuptools/command/saveopts.py
    setuptools/command/sdist.py
    setuptools/command/setopt.py
    setuptools/command/test.py
    setuptools/command/upload.py
    setuptools/command/upload_docs.py
    setuptools/compat/__init__.py
    setuptools/compat/py310.py
    setuptools/compat/py311.py
    setuptools/compat/py39.py
    setuptools/config/__init__.py
    setuptools/config/_apply_pyprojecttoml.py
    setuptools/config/_validate_pyproject/__init__.py
    setuptools/config/_validate_pyproject/error_reporting.py
    setuptools/config/_validate_pyproject/extra_validations.py
    setuptools/config/_validate_pyproject/fastjsonschema_exceptions.py
    setuptools/config/_validate_pyproject/fastjsonschema_validations.py
    setuptools/config/_validate_pyproject/formats.py
    setuptools/config/expand.py
    setuptools/config/pyprojecttoml.py
    setuptools/config/setupcfg.py
    setuptools/dep_util.py
    setuptools/depends.py
    setuptools/discovery.py
    setuptools/dist.py
    setuptools/errors.py
    setuptools/extension.py
    setuptools/extern/__init__.py
    setuptools/glob.py
    setuptools/installer.py
    setuptools/launch.py
    setuptools/logging.py
    setuptools/modified.py
    setuptools/monkey.py
    setuptools/msvc.py
    setuptools/namespaces.py
    setuptools/package_index.py
    setuptools/sandbox.py
    setuptools/unicode_utils.py
    setuptools/version.py
    setuptools/warnings.py
    setuptools/wheel.py
    setuptools/windows_support.py
)

RESOURCE_FILES(
    PREFIX contrib/python/setuptools/py3/
    .dist-info/METADATA
    .dist-info/entry_points.txt
    .dist-info/top_level.txt
    pkg_resources/_vendor/importlib_resources/py.typed
    pkg_resources/_vendor/more_itertools/py.typed
    pkg_resources/_vendor/packaging/py.typed
    pkg_resources/_vendor/platformdirs/py.typed
    setuptools/_vendor/importlib_metadata/py.typed
    setuptools/_vendor/importlib_resources/py.typed
    setuptools/_vendor/more_itertools/py.typed
    setuptools/_vendor/packaging/py.typed
    setuptools/_vendor/tomli/py.typed
    setuptools/script.tmpl
)

END()
