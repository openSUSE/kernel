#!/bin/bash

function get_config() {
	local line

	line=$(grep "^${1}=" include/config/auto.conf)
	if [ -z "$line" ]; then
		return
	fi
	echo "${line#*=}"
}

if [ ! -e include/config/auto.conf ]; then
        echo "Error: auto.conf not generated - run 'make prepare' to create it" >&2
	exit 1
fi

VERSION=$(get_config CONFIG_SUSE_VERSION)
PATCHLEVEL=$(get_config CONFIG_SUSE_PATCHLEVEL)
AUXRELEASE=$(get_config CONFIG_SUSE_AUXRELEASE)
PRODUCT_CODE=$(get_config CONFIG_SUSE_PRODUCT_CODE)

if [ -z "$VERSION" -o -z "$PATCHLEVEL" -o -z "$AUXRELEASE" ]; then
	# This would be a bug in the Kconfig
	cat <<- END >&2
	ERROR: Missing VERSION, PATCHLEVEL, or AUXRELEASE."
	Please check init/Kconfig.suse for correctness.
	END
	exit 1
fi

if [ "$VERSION" = 255 -o "$PATCHLEVEL" = 255 ]; then
	cat <<- END >&2

	ERROR: This release needs to be properly configured.
	Please add real values for SUSE_VERSION and SUSE_PATCHLEVEL.

	END
	exit 1
fi


case "$PRODUCT_CODE" in
	1)
		if [ "${PATCHLEVEL}" = "0" ]; then
			SP=""
		else
			SP="${PATCHLEVEL}"
		fi
		SUSE_PRODUCT_NAME="SUSE Linux Enterprise ${VERSION}${SP:+ SP}${SP}"
		SUSE_PRODUCT_SHORTNAME="SLE${VERSION}${SP:+-SP}${SP}"
		SUSE_PRODUCT_FAMILY="SLE"
		;;
	2)
		SUSE_PRODUCT_NAME="openSUSE Leap ${VERSION}.${PATCHLEVEL}"
		SUSE_PRODUCT_SHORTNAME="$SUSE_PRODUCT_NAME"
		SUSE_PRODUCT_FAMILY="Leap"
		;;
	3)
		SUSE_PRODUCT_NAME="openSUSE Tumbleweed"
		SUSE_PRODUCT_SHORTNAME="$SUSE_PRODUCT_NAME"
		SUSE_PRODUCT_FAMILY="Tumbleweed"
		;;
	4)
		SUSE_PRODUCT_NAME="SUSE Adaptable Linux Platform ${VERSION}.${PATCHLEVEL}"
		SUSE_PRODUCT_SHORTNAME="SLFO-${VERSION}.${PATCHLEVEL}"
		SUSE_PRODUCT_FAMILY="SLFO"
		;;
	*)
		echo "Unknown SUSE_PRODUCT_CODE=${PRODUCT_CODE}" >&2
		exit 1
		;;
esac

SUSE_PRODUCT_CODE=$(( (${PRODUCT_CODE} << 24) + \
		      (${VERSION} << 16) + (${PATCHLEVEL} << 8) + \
		       ${AUXRELEASE} ))

cat <<END
#ifndef _SUSE_VERSION_H
#define _SUSE_VERSION_H

#define SUSE_PRODUCT_CODE_SLE				1
#define SUSE_PRODUCT_CODE_OPENSUSE_LEAP			2
#define SUSE_PRODUCT_CODE_OPENSUSE_TUMBLEWEED		3
#define SUSE_PRODUCT_CODE_SLFO				4

#define SUSE_PRODUCT_FAMILY     "${SUSE_PRODUCT_FAMILY}"
#define SUSE_PRODUCT_NAME       "${SUSE_PRODUCT_NAME}"
#define SUSE_PRODUCT_SHORTNAME  "${SUSE_PRODUCT_SHORTNAME}"
#define SUSE_VERSION            ${VERSION}
#define SUSE_PATCHLEVEL         ${PATCHLEVEL}
#define SUSE_AUXRELEASE		${AUXRELEASE}
#define SUSE_PRODUCT_CODE       ${SUSE_PRODUCT_CODE}
#define SUSE_PRODUCT(product, version, patchlevel, auxrelease)		\\
	(((product) << 24) + ((version) << 16) +			\\
	 ((patchlevel) << 8) + (auxrelease))

#endif /* _SUSE_VERSION_H */
END
