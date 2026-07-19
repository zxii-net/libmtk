m_dep = meson.get_compiler('c').find_library('m', required: false)

tut_examples = [
  ['tut-01-hello', '01-hello.c'],
  ['tut-02-counter', '02-counter.c'],
  ['tut-03-convert', '03-convert.c'],
  ['tut-03-tasks', '03-tasks.c'],
  ['tut-04-lights', '04-lights.c'],
  ['tut-05-notes', '05-notes.c'],
  ['tut-06-pager', '06-pager.c'],
  ['tut-07-dirtree', '07-dirtree.c'],
  ['tut-08-dusage', '08-dusage.c'],
  ['tut-09-pixels', '09-pixels.c'],
  ['tut-a-mfm', 'a-mfm.c'],
  ['tut-b-tailview', 'b-tailview.c'],
  ['tut-c-mpaint', 'c-mpaint.c'],
]

foreach e : tut_examples
  executable(e[0], e[1], dependencies: [mtk_dep, m_dep])
endforeach
