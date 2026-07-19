project(
  'mtk',
  'c',
  version: '0.2.0',
  license: 'LGPL-2.1-only',
  default_options: [
    'c_std=c23',
    'warning_level=2',
    'buildtype=debugoptimized',
    'default_library=both',
  ],
)

add_project_arguments(
  '-D_POSIX_C_SOURCE=200809L', # clock_gettime, poll under strict C23
  language: 'c',
)

x11_dep = dependency('x11')
xrender_dep = dependency('xrender')

mtk_inc = include_directories('include')

mtk_sources = files(
  'src/core.c',
  'src/draw.c',
  'src/button.c',
  'src/label.c',
  'src/entry.c',
  'src/spinbox.c',
  'src/scrollbar.c',
  'src/tabs.c',
  'src/listbox.c',
  'src/tree.c',
  'src/sash.c',
  'src/frame.c',
  'src/layout.c',
  'src/menubar.c',
  'src/filedlg.c',
)

# shared + static (default_library=both); the shared library keeps
# LGPL relinking trivial for applications
libmtk = library(
  'mtk',
  mtk_sources,
  include_directories: mtk_inc,
  dependencies: [x11_dep, xrender_dep],
  # library (ABI) version; soname bumped for 0.2.0 when
  # MtkWidgetOps grew the `measure` member
  version: '1.0.0',
  install: true,
)

install_headers('include/mtk/mtk.h', subdir: 'mtk')

mtk_dep = declare_dependency(
  link_with: libmtk,
  include_directories: mtk_inc,
  dependencies: [x11_dep, xrender_dep],
)

# lets parent projects resolve dependency('mtk') to this subproject
meson.override_dependency('mtk', mtk_dep)

pkg = import('pkgconfig')
pkg.generate(
  libmtk,
  name: 'mtk',
  description: 'Motif-style widget toolkit on Xlib',
  requires: ['x11', 'xrender'],
)

# tutorial examples only when built directly, not as a subproject
if not meson.is_subproject()
  subdir('tutorial/examples')
endif
