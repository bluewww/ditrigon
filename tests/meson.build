if not get_option('gtk4-frontend')
  warning('fuzz-tests enabled but gtk4-frontend is disabled; skipping parser fuzz smoke test')
elif host_machine.system() == 'darwin'
  warning('fuzz-tests are only supported on Linux; skipping parser fuzz smoke test')
else
  if libssl_dep.found()
    security_poc_exe = executable('common-security-poc',
      ['common_security_poc.c', '../src/common/ssl.c', '../src/common/scram.c'],
      include_directories: [
        config_h_include,
        include_directories('../src/common'),
      ],
      dependencies: [
        glib_dep,
        libgio_dep,
        libssl_dep,
      ],
      c_args: ['-DHAVE_CONFIG_H'],
      build_by_default: false,
    )

    test('Common Security PoC', security_poc_exe,
      suite: ['security-poc'],
      is_parallel: false,
      timeout: 30,
    )
  endif

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

    safe_strcpy_cfg_script = files('safe_strcpy_cfg_utf8_regression.py')
    test('Safe Strcpy Config UTF-8 Regression', python3,
      args: [safe_strcpy_cfg_script, ditrigon_gtk4_exe],
      depends: [ditrigon_gtk4_exe],
      env: [
        'ASAN_OPTIONS=detect_leaks=0:halt_on_error=1',
        'UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1',
      ],
      suite: ['fuzz'],
      is_parallel: false,
      timeout: 90,
    )

    dcc_passive_hijack_script = files('dcc_passive_hijack_security_poc.py')
    test('DCC Passive Hijack Security PoC', python3,
      args: [dcc_passive_hijack_script, ditrigon_gtk4_exe],
      depends: [ditrigon_gtk4_exe],
      env: [
        'ASAN_OPTIONS=detect_leaks=0:halt_on_error=1',
        'UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1',
      ],
      suite: ['security-poc'],
      is_parallel: false,
      timeout: 90,
    )
  endif
endif
