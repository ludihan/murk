project('murk', 'c',
  version: '0.1',
  default_options: ['c_std=c11', 'warning_level=2', 'buildtype=release'])

is_web = host_machine.system() == 'emscripten'

cmake = import('cmake')
opts = cmake.subproject_options()
opts.add_cmake_defines({
  'BUILD_EXAMPLES': 'OFF',
  'CUSTOMIZE_BUILD': 'ON',
  'BUILD_SHARED_LIBS': 'OFF',
  'PLATFORM': is_web ? 'Web' : 'Desktop',
  'GRAPHICS': is_web ? 'GRAPHICS_API_OPENGL_ES3' : 'GRAPHICS_API_OPENGL_33',
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
if is_web
  # Box3D only turns these on when CMake sees the Emscripten toolchain, which Meson's cmake bridge doesn't use
  b3_opts.append_compile_args('c', '-msimd128', '-msse2')
endif
box3d_proj = cmake.subproject('box3d', options: b3_opts)
box3d_dep = box3d_proj.dependency('box3d')

stc_dep = dependency('stc')
m_dep = meson.get_compiler('c').find_library('m', required: false)

web_args = []
if is_web
  web_args = [
    '-sUSE_GLFW=3', '-sMIN_WEBGL_VERSION=2', '-sMAX_WEBGL_VERSION=2',
    '-sALLOW_MEMORY_GROWTH=1', '-sINITIAL_MEMORY=134217728', '-sSTACK_SIZE=1048576',
    '-sEXPORTED_FUNCTIONS=_main', '-sEXPORTED_RUNTIME_METHODS=UTF8ToString,stringToUTF8,HEAPF32,HEAP32,HEAPU8',
    '--shell-file', meson.project_source_root() / 'web/shell.html',
  ]
endif

executable('murk',
  ['src/main.c', 'src/world.c', 'src/gfx.c', 'src/player.c', 'src/audio.c', 'src/figure.c'],
  dependencies: [raylib_dep, box3d_dep, stc_dep, m_dep],
  c_args: is_web ? ['-sUSE_GLFW=3'] : [],
  link_args: web_args,
  name_suffix: is_web ? 'html' : [])
