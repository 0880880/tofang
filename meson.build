project('tofang', 'cpp',
  default_options : ['cpp_std=c++23',
    'warning_level=3',
    'cpp_args=-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast -Wundef -Wdouble-promotion -Wnon-virtual-dtor -Woverloaded-virtual -Wuseless-cast -Wformat=2 -Wnull-dereference -Wimplicit-fallthrough=5'
])

inc = include_directories(
  'include',
  'C:/mingw64/include',
  'C:/mingw64/x86_64-w64-mingw32/include'
)

src = files(
  'src/decl.cpp',
  'src/lexer.cpp',
  'src/parser.cpp',
  'src/symbols.cpp',
  'src/territory.cpp',
  'src/type.cpp',
  'src/typechecker.cpp',
  'main.cpp',
)

executable(
  'app',
  src,
  include_directories: inc,
  )
