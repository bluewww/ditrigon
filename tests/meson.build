if not get_option('gtk4-frontend')
  warning('fuzz-tests enabled but gtk4-frontend is disabled; skipping parser fuzz smoke test')
elif host_machine.system() == 'darwin'
  warning('fuzz-tests are only supported on Linux; skipping parser fuzz smoke test')
else
  python3 = find_program('python3', required: false)
  xvfb_run = find_program('xvfb-run', required: false)

  if not python3.found() or not xvfb_run.found()
    warning('fuzz-tests requested but python3 or xvfb-run was not found; skipping parser fuzz smoke test')
  else
    fuzz_script = files('fuzz_parser_smoke.py')

    test('Parser Fuzz Smoke', python3,
      args: [fuzz_script, ditrigon_gtk4_exe],
      depends: [ditrigon_gtk4_exe],
      env: [
        'ASAN_OPTIONS=detect_leaks=0:halt_on_error=1',
        'UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1',
      ],
      suite: ['fuzz'],
      is_parallel: false,
      timeout: 180,
    )

    utf8_regression_script = files('parser_utf8_truncation_regression.py')
    test('Parser UTF-8 Truncation Regression', python3,
      args: [utf8_regression_script, ditrigon_gtk4_exe],
      depends: [ditrigon_gtk4_exe],
      env: [
        'ASAN_OPTIONS=detect_leaks=0:halt_on_error=1',
        'UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1',
      ],
      suite: ['fuzz'],
      is_parallel: false,
      timeout: 90,
    )

    servlist_nick_overflow_script = files('servlist_nick_overflow_regression.py')
    test('Servlist Nick Overflow Regression', python3,
      args: [servlist_nick_overflow_script, ditrigon_gtk4_exe],
      depends: [ditrigon_gtk4_exe],
      env: [
        'ASAN_OPTIONS=detect_leaks=0:halt_on_error=1',
        'UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1',
      ],
      suite: ['fuzz'],
      is_parallel: false,
      timeout: 90,
    )
  endif
endif
