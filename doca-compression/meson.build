
project('DOCA_COMPRESSION', 'C', 'CPP',
	version: '1.0.0',
	license: 'Proprietary',
	default_options: ['buildtype=debug'],
	meson_version: '>= 0.61.2'
)

DOCA_PREFIX = 'doca_'

languages = ['c', 'cpp']

install_apps = false
bin_install_dir = ''

base_c_args = [
	'-Wno-missing-braces',
	'-Wno-missing-field-initializers',
]

base_cpp_args = [
	'-std=c++11',
	'-Wno-missing-field-initializers',
]

# Comment this line to restore warnings of experimental DOCA features
add_project_arguments('-D DOCA_ALLOW_EXPERIMENTAL_API', language: languages)

if get_option('enable_developer_log')
	add_project_arguments('-D DOCA_LOGGING_ALLOW_DLOG', language: languages)
endif

flag_enable_driver_flexio = get_option('enable_driver_flexio')

if not flag_enable_driver_flexio
	set_variable('flexio_disable_reason', 'Disabled by user')
endif


# Resolve irrelevant compiler warnings
add_project_arguments('-Wno-format-zero-length', language: languages)

base_app_dependencies = []
base_app_dependencies += dependency('threads')
base_app_dependencies += dependency('json-c')
dependency_libbsd = dependency('libbsd', required: false)
if dependency_libbsd.found()
	base_app_dependencies += dependency_libbsd
	add_project_arguments('-D DOCA_USE_LIBBSD', language: languages)
	# Ensure mlnx-dpdk will manage to find our libbsd if exists
	add_project_arguments('-D RTE_USE_LIBBSD', language: languages)
endif

doca_applications_path = '/opt/mellanox/doca/applications/'
common_path = 'common/src'
common_dir_path = doca_applications_path + common_path

base_app_inc_dirs = [
	include_directories(common_dir_path),
	include_directories(doca_applications_path + '..'),
]
samples_dir_path = doca_applications_path + '../samples'

source_path = 'src'
bin_path = 'bin'

app_inc_dirs = base_app_inc_dirs
app_libs = []


app_dependencies = base_app_dependencies
app_dependencies += dependency('doca')
app_dependencies += dependency('zlib')

app_srcs = []
app_srcs += [
	'compression_local.c',
	common_dir_path + '/pack.c',
	common_dir_path + '/utils.c',
	samples_dir_path + '/common.c',
]

vanilla_app_srcs = [
	'compression_local' + '.c',
]

executable(DOCA_PREFIX + 'compression_local',
	app_srcs + vanilla_app_srcs,
	c_args : base_c_args,
	dependencies : app_dependencies,
	include_directories : app_inc_dirs,
	install: install_apps)