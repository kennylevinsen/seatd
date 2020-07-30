project(
	'seatd',
	'c',
	version: '0.1',
	meson_version: '>=0.47.0',
	default_options: [
		'c_std=c11',
		'warning_level=3',
		'werror=true',
	],
)

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
		'-Wno-unknown-warning',
		'-Wvla',
		'-D_XOPEN_SOURCE=9000',
		'-Wl,--exclude-libs=ALL',
		'-fvisibility=hidden',
	],
	language: 'c',
)

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

if get_option('buildtype').startswith('debug')
	add_project_arguments('-DDEBUG', language : 'c')
endif


add_project_arguments(
	'-DLIBSEAT_REL_SRC_DIR="@0@"'.format(join_paths(relative_dir_parts) + '/'),
	language: 'c',
)

private_files = [
	'common/connection.c',
	'common/list.c',
	'common/log.c',
]

private_deps = []

server_files = [
	'common/log.c',
	'common/list.c',
	'common/terminal.c',
	'common/connection.c',
	'common/evdev.c',
	'common/drm.c',
	'seatd/poll/basic_poller.c',
	'seatd/poll/poller.c',
	'seatd/seat.c',
	'seatd/client.c',
	'seatd/server.c',
]

if get_option('seatd').enabled()
	private_files += 'libseat/backend/seatd.c'
	add_project_arguments('-DSEATD_ENABLED=1', language: 'c')
endif

if get_option('logind').enabled()
	logind = dependency('libsystemd', required: false)
	add_project_arguments('-DLOGIND_ENABLED=1', language: 'c')
	if logind.found()
		add_project_arguments('-DHAVE_SYSTEMD=1', language: 'c')
	else
		logind = dependency('libelogind')
		add_project_arguments('-DHAVE_ELOGIND=1', language: 'c')
	endif

	private_files += [
		'libseat/backend/logind.c',
		'common/drm.c',
	]
	private_deps += logind
endif

if get_option('builtin').enabled()
	add_project_arguments('-DBUILTIN_ENABLED=1', language: 'c')
	private_files += server_files
endif

private_lib = static_library(
	'seat-private',
	private_files,
	dependencies: private_deps,
	include_directories: [include_directories('.', 'include')],
)

lib = library(
	'seat', # This results in the library being called 'libseat'
	[ 'libseat/libseat.c' ],
	link_with: private_lib,
	include_directories: [include_directories('.', 'include')],
	install: true,
)

install_headers('include/libseat.h')

pkgconfig = import('pkgconfig')
pkgconfig.generate(lib,
	version: meson.project_version(),
	filebase: 'libseat',
	name: 'libseat',
	description: 'Seat management library',
)

if get_option('server').enabled()
	executable(
		'seatd',
		[ server_files, 'seatd/seatd.c' ],
		include_directories: [include_directories('.', 'include')],
		install: true,
	)
endif

if get_option('examples').enabled()
	executable(
		'simpletest',
		['examples/simpletest/main.c'],
		link_with: [lib],
		include_directories: [include_directories('.', 'include')],
		install: false,
	)
endif
