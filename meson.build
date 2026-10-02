project('murk', 'c',
  version: '0.1',
  default_options: ['c_std=c11', 'warning_level=2', 'buildtype=release'])

cmake = import('cmake')
opts = cmake.subproject_options()
opts.add_cmake_defines({
  'BUILD_EXAMPLES': 'OFF',
  'CUSTOMIZE_BUILD': 'ON',
  'BUILD_SHARED_LIBS': 'OFF',
  'PLATFORM': 'Desktop',
})
raylib_proj = cmake.subproject('raylib', options: opts)
raylib_dep = raylib_proj.dependency('raylib')

b3_opts = cmake.subproject_options()
b3_opts.add_cmake_defines({
  'BOX3D_SAMPLES': 'OFF',
  'BOX3D_UNIT_TESTS': 'OFF',
  'BOX3D_BENCHMARKS': 'OFF',
  'BOX3D_DOCS': 'OFF',
  'BOX3D_VALIDATE': 'OFF',
  'BUILD_SHARED_LIBS': 'OFF',
})
box3d_proj = cmake.subproject('box3d', options: b3_opts)
box3d_dep = box3d_proj.dependency('box3d')

stc_dep = dependency('stc')
m_dep = meson.get_compiler('c').find_library('m', required: false)

executable('murk',
  ['src/main.c', 'src/world.c', 'src/gfx.c', 'src/player.c', 'src/audio.c'],
  dependencies: [raylib_dep, box3d_dep, stc_dep, m_dep])
