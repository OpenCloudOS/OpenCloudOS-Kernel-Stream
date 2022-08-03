#!/bin/bash --norc
# SPDX-License-Identifier: GPL-2.0

# shellcheck source=./lib-version.sh
. "/$(dirname "$(realpath "$0")")/lib-version.sh"

usage()
{
	cat << EOF
gen-spec.sh [OPTION]

	--kernel-dist		Kernel distribution marker, eg. tks/tlinux4/tlinux3
	--kernel-config		Kernel config base name, will look for the corresponding kernel config under $DISTDIR/configs
	--kernel-variant	Kernel variant, eg. debug, gcov, kdb
	--build-arch		What arch's are supported, defaults to '$SPEC_ARCH'"
EOF
}

while [[ $# -gt 0 ]]; do
	case $1 in
		--kernel-config )
			KERNEL_CONFIG=$2
			shift 2
			;;
		--kernel-variant )
			KERNEL_VARIANT=$2
			shift 2
			;;
		--build-arch )
			BUILD_ARCH=$2
			shift 2
			;;
		--commit )
			COMMIT=$2
			shift 2
			;;
		* )
			die "Unrecognized parameter $1"
			;;
	esac
done

# This function will prepare $KERNEL_MAJVER, $KERNEL_RELVER
prepare_kernel_ver "${COMMIT:-HEAD}"

BUILD_ARCH="${BUILD_ARCH:-$SPEC_ARCH}"

RPM_NAME="kernel${KERNEL_VARIANT:+-$KERNEL_VARIANT}${KERNEL_DIST:+-$KERNEL_DIST}"
RPM_VERSION=${KERNEL_MAJVER//-/.}
RPM_RELEASE=${KERNEL_RELVER//-/.}
RPM_RELEASE=${RPM_RELEASE%".$KERNEL_DIST"}
RPM_VENDOR=$(get_dist_makefile_var VENDOR_CAPITALIZED)
RPM_URL=$(get_dist_makefile_var URL)

if [ -z "$KERNEL_CONFIG" ]; then
	for config in "$TOPDIR/configs/"*.config; do
		# "<config-basename>.<arch>.config" -> "<config-basename>"
		KERNEL_CONFIG=$(basename "$config")
		KERNEL_CONFIG=${config%.config}
		KERNEL_CONFIG=${config%.*}
		break
	done

	if [ -z "$KERNEL_CONFIG" ]; then
		die "There is no valid kernel config."
	fi
fi

_gen_arch_spec() {
	local arch kernel_arch
	cat << EOF
ExclusiveArch: $BUILD_ARCH
EOF
	for arch in $BUILD_ARCH; do
		if ! kernel_arch=$(get_kernel_arch "$arch"); then
			die "Unsupported arch '$arch'"
		fi
		cat << EOF
%ifarch $arch
%define kernel_arch $kernel_arch
%endif
EOF
	done
}

_gen_kerver_spec() {
	cat << EOF
%define rpm_name $RPM_NAME
%define rpm_version $RPM_VERSION
%define rpm_release $RPM_RELEASE
%define rpm_vendor $RPM_VENDOR
%define rpm_url $RPM_URL
%define kernel_majver $KERNEL_MAJVER
%define kernel_relver $KERNEL_RELVER
EOF
[ "$KERNEL_VARIANT" ] && cat << EOF
%define kernel_variant $KERNEL_VARIANT
EOF
}

_gen_arch_source() {
	# Source1000 - Source1199 for kernel config
	local config_source_num=1000 arch
	for arch in $BUILD_ARCH; do
		echo "Source$config_source_num: $KERNEL_CONFIG.$arch.config"
		config_source_num=$((config_source_num + 1))
	done

	# Source1200 - Source1399 for kabi source
	local kabi_source_num=1200 arch
	for arch in $BUILD_ARCH; do
		echo "Source$kabi_source_num: Module.kabi_$arch"
		kabi_source_num=$((kabi_source_num + 1))
	done

	# Source1400 - Source1599 for module filter
	local filter_source_num=1400 arch
	for arch in $BUILD_ARCH; do
		echo "Source$filter_source_num: filter-$arch.sh"
		filter_source_num=$((filter_source_num + 1))
	done
}

_gen_config_build() {
	cat << EOF
%ifnarch $BUILD_ARCH
{error:unsupported arch}
%endif
EOF
	local config_source_num=1000 arch
	for arch in $BUILD_ARCH; do
		cat << EOF
%ifarch $arch
BuildConfig %{SOURCE$config_source_num}
%endif
EOF
		config_source_num=$((config_source_num + 1))
	done
}

_gen_kabi_check() {
	cat << EOF
%ifnarch $BUILD_ARCH
{error:unsupported arch}
%endif
EOF
	local kabi_source_num=1200 arch
	for arch in $BUILD_ARCH; do
		cat << EOF
%ifarch $arch
CheckKernelABI %{SOURCE$kabi_source_num}
%endif
EOF
		kabi_source_num=$((kabi_source_num + 1))
	done
}

_gen_changelog_spec() {
	cat "$DISTDIR/templates/changelog"
}

gen_spec() {
	local _line
	local _spec

	while IFS='' read -r _line; do
		case $_line in
			"{{ARCHSPEC}}"* )
				_spec+="$(_gen_arch_spec)"
				;;
			"{{VERSIONSPEC}}"* )
				_spec+="$(_gen_kerver_spec)"
				;;
			"{{ARCHSOURCESPEC}}"* )
				_spec+="$(_gen_config_source)"
				;;
			"{{CONFBUILDSPEC}}"* )
				_spec+="$(_gen_config_build)"
				;;
			"{{KABICHECKSPEC}}"* )
				_spec+="$(_gen_kabi_check)"
				;;
			"{{CHANGELOGSPEC}}"* )
				_spec+="$(_gen_changelog_spec)"
				;;
			"{{"*"}}"*)
				die "Unrecognized template keywork $_line"
				;;
			* )
				_spec+="$_line"
				;;
		esac
		_spec+="
"
	done

	echo "$_spec"
}

gen_spec < "$DISTDIR/templates/kernel.template.spec"
