project(
	'seatd',
	'c',
	version: '0.5.0',
	license: 'MIT',
	meson_version: '>=0.56.0',
	default_options: [
		'c_std=c11',
		'warning_level=3',
		'werror=true',
	],
)

# Bump whenever ABI-breaking changes occur.
libseat_soversion = 1

defaultpath = get_option('defaultpath')
if defaultpath == ''
	defaultpath = '/var/run/seatd.sock'
	if target_machine.system() == 'linux'
	   defaultpath = '/run/seatd.sock'
	endif
endif

add_project_arguments(
	[
		'-Wundef',
		'-Wunused',
		'-Wlogical-op',
		'-Wmissing-include-dirs',
		'-Wold-style-definition', # nop
		'-Wpointer-arith',
		'-Wstrict-prototypes',
		'-Wimplicit-fallthrough',
		'-Wmissing-prototypes',
		'-Wno-unknown-warning-option',
		'-Wno-unused-command-line-argument',
		'-Wvla',
		'-Wl,--exclude-libs=ALL',
		'-D_XOPEN_SOURCE=700',
		'-D__BSD_VISIBLE',
		'-DSEATD_VERSION="@0@"'.format(meson.project_version()),
		'-DSEATD_DEFAULTPATH="@0@"'.format(defaultpath)
	],
	language: 'c',
)

if ['debugoptimized', 'release', 'minsize'].contains(get_option('buildtype'))
	add_project_arguments('-D_FORTIFY_SOURCE=2', language: 'c')
endif

if get_option('buildtype').startswith('debug')
	add_project_arguments('-DDEBUG', language : 'c')
endif

# Hacks
source_root = meson.current_source_dir().split('/')
build_root = meson.build_root().split('/')
relative_dir_parts = []
i = 0
in_prefix = true
foreach p : build_root
	if i >= source_root.length() or not in_prefix or p != source_root[i]
		in_prefix = false
		relative_dir_parts += '..'
	endif
	i += 1
endforeach
i = 0
in_prefix = true
foreach p : source_root
	if i >= build_root.length() or not in_prefix or build_root[i] != p
		in_prefix = false
		relative_dir_parts += p
	endif
	i += 1
endforeach

add_project_arguments(
	'-DREL_SRC_DIR="@0@"'.format(join_paths(relative_dir_parts) + '/'),
	language: 'c',
)

private_files = [
	'common/connection.c',
	'common/linked_list.c',
	'common/log.c',
]

private_deps = []

server_files = [
	'common/log.c',
	'common/linked_list.c',
	'common/terminal.c',
	'common/connection.c',
	'common/evdev.c',
	'common/drm.c',
	'seatd/poller.c',
	'seatd/seat.c',
	'seatd/client.c',
	'seatd/server.c',
]

with_seatd = get_option('libseat-seatd') == 'enabled'
with_builtin = get_option('libseat-builtin') == 'enabled'
with_server = get_option('server') == 'enabled'

if with_seatd or with_builtin
	private_files += 'libseat/backend/seatd.c'
endif

if with_seatd
	add_project_arguments('-DSEATD_ENABLED=1', language: 'c')
endif

logind = disabler()
logind_provider = ''
if get_option('libseat-logind') != 'disabled'
	if get_option('libseat-logind') == 'auto' and get_option('auto_features').disabled()
		# Disable logind
	elif get_option('libseat-logind') == 'auto'
		assert(get_option('auto_features').auto(), '-Dlibseat-logind must be set to systemd or elogind since auto_features != auto')
		foreach logind_provider : ['elogind', 'systemd']
			logind = dependency('lib@0@'.format(logind_provider), required: false)
			if logind.found()
				break
			endif
		endforeach
	else
		logind_provider = get_option('libseat-logind')
		logind = dependency('lib@0@'.format(logind_provider))
	endif

	if logind.found()
		add_project_arguments('-DLOGIND_ENABLED=1', language: 'c')
		add_project_arguments('-DHAVE_@0@=1'.format(logind_provider.to_upper()), language: 'c')
		private_files += [
			'libseat/backend/logind.c',
			'common/drm.c',
		]
		private_deps += logind
	endif
endif

# needed for cross-compilation
realtime = meson.get_compiler('c').find_library('rt')
private_deps += realtime

if with_builtin
	add_project_arguments('-DBUILTIN_ENABLED=1', language: 'c')
	private_files += server_files
endif

private_lib = static_library(
	'seat-private',
	private_files,
	dependencies: private_deps,
	include_directories: [include_directories('.', 'include')],
)

symbols_file = 'libseat/libseat.syms'
symbols_flag = '-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(), symbols_file)
lib = library(
	'seat', # This results in the library being called 'libseat'
	[ 'libseat/libseat.c', 'libseat/backend/noop.c' ],
	soversion: libseat_soversion,
	link_with: private_lib,
	include_directories: [include_directories('.', 'include')],
	install: true,
	link_args: symbols_flag,
	link_depends: symbols_file,
)

install_headers('include/libseat.h')

libseat_vars = {
	'have_seatd': with_seatd.to_string(),
	'have_logind': logind.found().to_string(),
	'have_builtin': with_builtin.to_string(),
}

pkgconfig = import('pkgconfig')
pkgconfig.generate(lib,
	version: meson.project_version(),
	filebase: 'libseat',
	name: 'libseat',
	description: 'Seat management library',
	variables: libseat_vars,
)

libseat = declare_dependency(
	link_with: lib,
	dependencies: private_deps,
	include_directories: include_directories('include', is_system: true),
	variables: libseat_vars,
)

if with_server
	executable(
		'seatd',
		[ server_files, 'seatd/seatd.c' ],
		include_directories: [include_directories('.', 'include')],
		install: true,
		dependencies: [realtime],
	)
endif

if get_option('examples') == 'enabled'
	executable(
		'simpletest',
		['examples/simpletest/main.c'],
		link_with: [lib],
		include_directories: [include_directories('.', 'include')],
		install: false,
	)
endif

tests = {
	'linked_list': ['common/linked_list.c'],
	'connection': ['common/connection.c'],
	'poller': ['common/linked_list.c', 'seatd/poller.c'],
}

foreach name, value : tests
	test(name, executable(
		'@0@_test'.format(name),
		['tests/@0@.c'.format(name), value],
		include_directories: [include_directories('.', 'include')]))
endforeach

if with_server
	scdoc = dependency('scdoc', required: get_option('man-pages'), version: '>= 1.9.7', native: true)
else
	scdoc = disabler()
endif

if scdoc.found()
	scdoc_prog = find_program(scdoc.get_variable(pkgconfig: 'scdoc'), native: true)
	mandir = get_option('mandir')

	foreach src : ['seatd.1.scd']
		topic = src.split('.')[0]
		section = src.split('.')[1]
		output = '@0@.@1@'.format(topic, section)

		custom_target(
			output,
			input: 'man/' + src,
			output: output,
			command: [
				'sh', '-c', '@0@ < @INPUT@ > @1@'.format(scdoc_prog.full_path(), output)
			],
			install: true,
			install_dir: '@0@/man@1@'.format(mandir, section)
		)
	endforeach
endif

summary({
	'libseat-seatd': with_seatd,
	'libseat-builtin': with_builtin,
	'libseat-systemd': logind.found() and logind_provider == 'systemd',
	'libseat-elogind': logind.found() and logind_provider == 'elogind',
	'server': with_server,
}, bool_yn: true)
