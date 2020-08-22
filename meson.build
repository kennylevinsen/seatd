project(
	'seatd',
	'c',
	version: '0.1',
	license: 'MIT',
	meson_version: '>=0.53.0',
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
		'-Wno-unknown-warning-option',
		'-Wno-unused-command-line-argument',
		'-Wvla',
		'-Wl,--exclude-libs=ALL',
		'-D_XOPEN_SOURCE=700',
		'-D__BSD_VISIBLE',
		'-DSEATD_VERSION="@0@"'.format(meson.project_version()),
	],
	language: 'c',
)

if ['debugoptimized', 'release', 'minsize'].contains(get_option('buildtype'))
   add_project_arguments('-D_FORTIFY_SOURCE=2', language: 'c')
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

if get_option('buildtype').startswith('debug')
	add_project_arguments('-DDEBUG', language : 'c')
endif


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
	'common/list.c',
	'common/terminal.c',
	'common/connection.c',
	'common/evdev.c',
	'common/drm.c',
	'seatd/poller.c',
	'seatd/seat.c',
	'seatd/client.c',
	'seatd/server.c',
]

if get_option('seatd').enabled()
	private_files += 'libseat/backend/seatd.c'
	add_project_arguments('-DSEATD_ENABLED=1', language: 'c')
endif

logind_provider = ''
if get_option('logind').enabled()
	# Check for libelogind first, as elogind may provide a libsystemd wrapper
	# which can cause issues.
	logind = dependency('libelogind', required: false)
	add_project_arguments('-DLOGIND_ENABLED=1', language: 'c')
	if logind.found()
		add_project_arguments('-DHAVE_ELOGIND=1', language: 'c')
		logind_provider = 'elogind'
	else
		logind = dependency('libsystemd')
		add_project_arguments('-DHAVE_SYSTEMD=1', language: 'c')
		logind_provider = 'systemd'
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

symbols_file = 'libseat/libseat.syms'
symbols_flag = '-Wl,--version-script,@0@/@1@'.format(meson.current_source_dir(), symbols_file)
lib = library(
	'seat', # This results in the library being called 'libseat'
	[ 'libseat/libseat.c' ],
	link_with: private_lib,
	include_directories: [include_directories('.', 'include')],
	install: true,
	link_args: symbols_flag,
	link_depends: symbols_file,
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

test(
        'linked_list',
        executable(
                'linked_list_test',
                ['common/linked_list.c', 'tests/linked_list.c'],
                include_directories: [include_directories('.', 'include')],
        )
)

scdoc = dependency('scdoc', required: get_option('man-pages'), version: '>= 1.9.7')

if scdoc.found()
	sh = find_program('sh')

	man_pages = ['seatd.1.scd']

	mandir = get_option('mandir')

	foreach src : man_pages
		topic = src.split('.')[0]
		section = src.split('.')[1]
		output = '@0@.@1@'.format(topic, section)

		custom_target(
			output,
			input: 'man/' + src,
			output: output,
			command: [
				sh, '-c', '@0@ < @INPUT@ > @1@'.format(scdoc.get_pkgconfig_variable('scdoc'), output)
			],
			install: true,
			install_dir: '@0@/man@1@'.format(mandir, section)
		)
	endforeach
endif

summary({
   'seatd': get_option('seatd').enabled() ? 1 : 0,
   'builtin': get_option('builtin').enabled() ? 1 : 0,
   'systemd': logind_provider == 'systemd' ? 1 : 0,
   'elogind': logind_provider == 'elogind' ? 1 : 0,
})
