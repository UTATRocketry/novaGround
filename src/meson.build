subdir('core')
subdir('utils')

# Everything below needs the Pi's hardware libraries.
if get_option('hardware')
  subdir('interfaces')
  subdir('controllers')
  src += core_src
  src += files('main.cpp')
endif
