ditrigon_gtk4_sources = [
  'adw.c',
  'ascii.c',
  'banlist.c',
  'chanlist.c',
  'chanview-tree.c',
  'chanview.c',
  'custom-list.c',
  'dccgui.c',
  'editlist.c',
  'fe-gtk4.c',
  'fkeys.c',
  'gtkutil.c',
  'ignoregui.c',
  'joind.c',
  'maingui.c',
  'menu.c',
  'notifygui.c',
  'palette.c',
  'pixmaps.c',
  'plugin-notification.c',
  'plugingui.c',
  'plugin-tray.c',
  'rawlog.c',
  'servlistgui.c',
  'sexy-iso-codes.c',
  'sexy-spell-entry.c',
  'setup.c',
  'textgui.c',
  'urlgrab.c',
  'userlistgui.c',
  'xtext.c',
]

gtk4_dep = dependency('gtk4', version: '>= 4.10')

ditrigon_gtk4_deps = [
  hexchat_common_dep,
  libgmodule_dep,
  gtk4_dep,
]

if host_machine.system() == 'windows'
  ditrigon_gtk4_sources += 'notifications/notification-dummy.c'
else
  ditrigon_gtk4_sources += 'notifications/notification-freedesktop.c'
endif

iso_codes = dependency('iso-codes', required: false)
ditrigon_gtk4_cflags = []
adwaita_dep = dependency('libadwaita-1')
ditrigon_gtk4_deps += adwaita_dep
if iso_codes.found()
  iso_codes_prefix = iso_codes.get_pkgconfig_variable('prefix')
  ditrigon_gtk4_cflags += '-DISO_CODES_PREFIX="@0@"'.format(iso_codes_prefix)
  ditrigon_gtk4_cflags += '-DISO_CODES_LOCALEDIR="@0@"'.format(
                        join_paths(iso_codes_prefix, 'share/locale'))
endif

resources = gnome.compile_resources('resources-gtk4',
  '../../data/ditrigon.gresource.xml',
  source_dir: '../../data',
  c_name: 'ditrigon',
  extra_args: ['--manual-register']
)

ditrigon_gtk4_exe = executable('ditrigon',
  sources: resources + ditrigon_gtk4_sources,
  dependencies: ditrigon_gtk4_deps,
  c_args: ditrigon_gtk4_cflags,
  install: true,
  gui_app: true,
)
