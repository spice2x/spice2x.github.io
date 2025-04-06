#!/bin/bash

# set to EN-US for consistency
LANG=en_us_8859_1

# exit on error
set -e
CMD_CURR="";
function trap_error_dbg {
    CMD_LAST=${CMD_CURR}
    CMD_CURR=$BASH_COMMAND
}
function trap_error_exit {
    if (($? > 0))
    then
        echo "\"${CMD_LAST}\" resulted in exit code $?."
    fi
}
trap trap_error_dbg DEBUG
trap trap_error_exit EXIT

# settings
GIT_BRANCH=$(git rev-parse --abbrev-ref HEAD 2> /dev/null || echo "none")
GIT_HEAD=$(git rev-parse HEAD || echo "none")
TOOLCHAIN_32="/usr/share/mingw/toolchain-i686-w64-mingw32.cmake"
TOOLCHAIN_64="/usr/share/mingw/toolchain-x86_64-w64-mingw32.cmake"
BUILDDIR_32_RELEASE="./cmake-build-release-32"
BUILDDIR_32_DEBUG="./cmake-build-debug-32"
BUILDDIR_64_RELEASE="./cmake-build-release-64"
BUILDDIR_64_DEBUG="./cmake-build-debug-64"
DEBUG=0
OUTDIR="./bin/spice2x"

# disabled UPX since it tends to falsely trigger malware detection
UPX_ENABLE=0
UPX_FLAGS="-q --best --lzma --compress-exports=0"

CLEAN_BUILD=1
INCLUDE_SRC=1
DIST_ENABLE=1
DIST_FOLDER="./dist"
DIST_NAME="spice2x-$(date +%y)-$(date +%m)-$(date +%d).zip"
DIST_COMMENT=${DIST_NAME}$'\n'"$GIT_BRANCH - $GIT_HEAD"$'\nThank you for playing.'
TARGETS_32="spicetools_stubs_kbt spicetools_stubs_kld spicetools_cfg spicetools_spice"
TARGETS_64="spicetools_stubs_kbt64 spicetools_stubs_kld64 spicetools_stubs_nvEncodeAPI64 spicetools_stubs_nvcuvid spicetools_stubs_nvcuda spicetools_spice64"

# determine build type
BUILD_TYPE="Release"
BUILDDIR_32=${BUILDDIR_32_RELEASE}
BUILDDIR_64=${BUILDDIR_64_RELEASE}
if ((DEBUG > 0))
then
	BUILD_TYPE="Debug"
	BUILDDIR_32=${BUILDDIR_32_DEBUG}
	BUILDDIR_64=${BUILDDIR_64_DEBUG}
	DIST_NAME=$(echo ${DIST_NAME} | sed 's/\.[^.]*$/-dbg&/')
fi

# determine number of cores
CORES=$(awk '/^processor\t/ {cores[$NF]++} END {print length(cores)}' /proc/cpuinfo)

# print information
echo ""
echo "SpiceTools Build-Script"
echo "======================="
echo "Host: $(uname -sro)"
echo "Date: $(date)"
echo "Git Branch: $GIT_BRANCH"
echo "Git Head: $GIT_HEAD"
echo "Toolchain for 32bit targets: $TOOLCHAIN_32"
echo "Toolchain for 64bit targets: $TOOLCHAIN_64"
echo "Distribution Name: $DIST_NAME"
echo "Build Type: $BUILD_TYPE"
echo "Cores: $CORES"
echo ""

time (
	# 32 bit
	echo "Building 32bit targets..."
	echo "========================="
	if ((CLEAN_BUILD > 0))
	then
		rm -rf ${BUILDDIR_32}
	fi
	mkdir -p ${BUILDDIR_32}
	pushd ${BUILDDIR_32} > /dev/null
	cmake -G "Ninja" -DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_32} -DCMAKE_BUILD_TYPE=${BUILD_TYPE} $OLDPWD && ninja ${TARGETS_32}
	popd > /dev/null

	# 64 bit
	echo ""
	echo "Building 64bit targets..."
	echo "========================="
	if ((CLEAN_BUILD > 0))
	then
		rm -rf ${BUILDDIR_64}
	fi
	mkdir -p ${BUILDDIR_64}
	pushd ${BUILDDIR_64} > /dev/null
	cmake -G "Ninja" -DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_64} -DCMAKE_BUILD_TYPE=${BUILD_TYPE} $OLDPWD && ninja ${TARGETS_64}
	popd > /dev/null

	echo ""
	echo "Compilation process done :)"
	echo "==========================="
)

# generate PDBs
if false  # ((DEBUG > 0))
then
	echo "Generating PDBs..."

	# start conversions in multiple processes if in target and binary newer than PDB
	if [[ ${TARGETS_32} == *"spicetools_cfg"* ]] && \
	        [[ ${BUILDDIR_32}/spicetools/spicecfg.exe -nt ${BUILDDIR_32}/spicetools/spicecfg-pdb.exe ]]; then
	    rm -f ${BUILDDIR_32}/spicetools/spicecfg-pdb.exe ${BUILDDIR_32}/spicetools/spicecfg-pdb.pdb
	    (wine external/cv2pdb/cv2pdb.exe \
	        ${BUILDDIR_32}/spicetools/spicecfg.exe \
	        ${BUILDDIR_32}/spicetools/spicecfg-pdb.exe \
	        ${BUILDDIR_32}/spicetools/spicecfg-pdb.pdb \
	        &> /dev/null && \
	        touch --reference=${BUILDDIR_32}/spicetools/spicecfg.exe ${BUILDDIR_32}/spicetools/spicecfg-pdb.exe && \
	        echo "Generation done for spicetools_cfg") &
	fi
	if [[ ${TARGETS_32} == *"spicetools_spice"* ]] && \
	        [[ ${BUILDDIR_32}/spicetools/32/spice.exe -nt ${BUILDDIR_32}/spicetools/32/spice-pdb.exe ]]; then
	    rm -f ${BUILDDIR_32}/spicetools/32/spice-pdb.exe ${BUILDDIR_32}/spicetools/32/spice-pdb.pdb
	    (wine external/cv2pdb/cv2pdb.exe \
	        ${BUILDDIR_32}/spicetools/32/spice.exe \
	        ${BUILDDIR_32}/spicetools/32/spice-pdb.exe \
	        ${BUILDDIR_32}/spicetools/32/spice-pdb.pdb \
	        &> /dev/null && \
	        touch --reference=${BUILDDIR_32}/spicetools/32/spice.exe ${BUILDDIR_32}/spicetools/32/spice-pdb.exe && \
	        echo "Generation done for spicetools_spice") &
	fi
	if [[ ${TARGETS_64} == *"spicetools_spice64"* ]] && \
	        [[ ${BUILDDIR_64}/spicetools/64/spice64.exe -nt ${BUILDDIR_64}/spicetools/64/spice64-pdb.exe ]]; then
	    rm -f ${BUILDDIR_64}/spicetools/64/spice64-pdb.exe ${BUILDDIR_64}/spicetools/64/spice64-pdb.pdb
	    (wine external/cv2pdb/cv2pdb.exe \
	        ${BUILDDIR_64}/spicetools/64/spice64.exe \
	        ${BUILDDIR_64}/spicetools/64/spice64-pdb.exe \
	        ${BUILDDIR_64}/spicetools/64/spice64-pdb.pdb \
	        &> /dev/null && \
	        touch --reference=${BUILDDIR_64}/spicetools/64/spice64.exe ${BUILDDIR_64}/spicetools/64/spice64-pdb.exe && \
	        echo "Generation done for spicetools_spice64") &
	fi

	# wait until all processes are complete
	wait
fi

# copy to output directory
echo "Copy files to output directory..."
rm -rf ${OUTDIR}
mkdir -p ${OUTDIR}
#mkdir -p ${OUTDIR}/stubs/32
mkdir -p ${OUTDIR}/stubs/64
if false # ((DEBUG > 0))
then
    # debug files
    cp ${BUILDDIR_32}/spicetools/spicecfg-pdb.exe ${OUTDIR} 2>/dev/null
    cp ${BUILDDIR_32}/spicetools/spicecfg-pdb.pdb ${OUTDIR} 2>/dev/null
    cp ${BUILDDIR_32}/spicetools/32/spice-pdb.exe ${OUTDIR} 2>/dev/null
    cp ${BUILDDIR_32}/spicetools/32/spice-pdb.pdb ${OUTDIR} 2>/dev/null
    #cp ${BUILDDIR_32}/spicetools/32/kbt.dll ${OUTDIR}/stubs/32 2>/dev/null
    #cp ${BUILDDIR_32}/spicetools/32/kld.dll ${OUTDIR}/stubs/32 2>/dev/null
    cp ${BUILDDIR_64}/spicetools/64/spice64-pdb.exe ${OUTDIR} 2>/dev/null
    cp ${BUILDDIR_64}/spicetools/64/spice64-pdb.pdb ${OUTDIR} 2>/dev/null
    #cp ${BUILDDIR_64}/spicetools/64/kbt.dll ${OUTDIR}/stubs/64 2>/dev/null
    #cp ${BUILDDIR_64}/spicetools/64/kld.dll ${OUTDIR}/stubs/64 2>/dev/null
    #cp ${BUILDDIR_64}/spicetools/64/nvEncodeAPI64.dll ${OUTDIR}/stubs/64 2>/dev/null
    #cp ${BUILDDIR_64}/spicetools/64/nvcuda.dll ${OUTDIR}/stubs/64 2>/dev/null
    #cp ${BUILDDIR_64}/spicetools/64/nvcuvid.dll ${OUTDIR}/stubs/64 2>/dev/null
else
    # release files
    cp ${BUILDDIR_32}/spicetools/spicecfg.exe ${OUTDIR} 2>/dev/null
    cp ${BUILDDIR_32}/spicetools/32/spice.exe ${OUTDIR} 2>/dev/null
    #cp ${BUILDDIR_32}/spicetools/32/kbt.dll ${OUTDIR}/stubs/32 2>/dev/null
    #cp ${BUILDDIR_32}/spicetools/32/kld.dll ${OUTDIR}/stubs/32 2>/dev/null
    cp ${BUILDDIR_64}/spicetools/64/spice64.exe ${OUTDIR} 2>/dev/null
    #cp ${BUILDDIR_64}/spicetools/64/kbt.dll ${OUTDIR}/stubs/64 2>/dev/null
    #cp ${BUILDDIR_64}/spicetools/64/kld.dll ${OUTDIR}/stubs/64 2>/dev/null
    cp ${BUILDDIR_64}/spicetools/64/nvEncodeAPI64.dll ${OUTDIR}/stubs/64 2>/dev/null
    cp ${BUILDDIR_64}/spicetools/64/nvcuda.dll ${OUTDIR}/stubs/64 2>/dev/null
    cp ${BUILDDIR_64}/spicetools/64/nvcuvid.dll ${OUTDIR}/stubs/64 2>/dev/null
fi

# pack source files to output directory
rm -rf ${OUTDIR}/src
mkdir -p ${OUTDIR}/src
if ((INCLUDE_SRC > 0))
then
	echo "Generating source file archive..."
	git archive --format tar.gz --prefix=spice2x/ HEAD ./ > ${OUTDIR}/src/spice2x-${GIT_BRANCH}.tar.gz 2>/dev/null || \
		echo "WARNING: Couldn't get git to create the archive. Is this a git repository?"
fi

# copy resources
rm -rf ${OUTDIR}/api
mkdir ${OUTDIR}/api
find ./api/resources/python -name "__pycache__" -exec rm -rf {} +
cp -r ./api/resources/* ${OUTDIR}/api

# build distribution archive
if ((DIST_ENABLE > 0))
then
	echo "Building dist..."
	mkdir -p ${DIST_FOLDER}
	rm -rf ${DIST_FOLDER}/${DIST_NAME}
	pushd ${OUTDIR}/.. > /dev/null
	zip -qrXT9 $OLDPWD/${DIST_FOLDER}/${DIST_NAME} . -z <<< "$DIST_COMMENT"
	popd > /dev/null
fi

# finish
echo "All done!"
exit 0
