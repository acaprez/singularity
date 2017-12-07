#!/bin/bash
#
# Copyright (c) 2017-2018, SyLabs, Inc. All rights reserved.
# Copyright (c) 2017, SingularityWare, LLC. All rights reserved.
# Copyright (c) 2015-2017, Gregory M. Kurtzer. All rights reserved.

function log_usage {
	local logger='/util/opt/bin/logger'
	local syslog_address='10.138.38.26'
	local syslog_port=5001
	local tag='singularity-crane'
	local user=${USER:='unknown'}
	local jobid=${SLURM_JOB_ID:='unknown'}
	local image=${NAME:='unknown'}
	local msg=`printf 'user=%s,image=%s,job=%s' "$user" "$image" "$jobid"`
	eval $logger -d -n $syslog_address -P $syslog_port -t $tag -p local0.info $msg > /dev/null 2>&1
}


NAME=`echo "$SINGULARITY_IMAGE" | sed -e 's@^docker://@@'`

if [ -z "${SINGULARITY_LOCALCACHEDIR:-}" ]; then
    SINGULARITY_LOCALCACHEDIR="${TMPDIR:-/tmp}"
fi

if ! SINGULARITY_TMPDIR=`mktemp -d $SINGULARITY_LOCALCACHEDIR/.singularity-runtime.XXXXXXXX`; then
    message ERROR "Failed to create temporary directory\n"
    ABORT 255
fi

SINGULARITY_ROOTFS="$SINGULARITY_TMPDIR/$NAME"
if ! mkdir -p "$SINGULARITY_ROOTFS"; then
    message ERROR "Failed to create named SINGULARITY_ROOTFS=$SINGULARITY_ROOTFS\n"
    ABORT 255
fi

SINGULARITY_CONTAINER="$SINGULARITY_IMAGE"
SINGULARITY_IMAGE="$SINGULARITY_ROOTFS"
SINGULARITY_CLEANUPDIR="$SINGULARITY_TMPDIR"
if ! SINGULARITY_CONTENTS=`mktemp ${TMPDIR:-/tmp}/.singularity-layers.XXXXXXXX`; then
    message ERROR "Failed to create temporary directory\n"
    ABORT 255
fi

export SINGULARITY_ROOTFS SINGULARITY_IMAGE SINGULARITY_CONTAINER SINGULARITY_CONTENTS SINGULARITY_CLEANUPDIR

eval_abort "$SINGULARITY_libexecdir/singularity/python/import.py"

log_usage
message 1 "Creating container runtime...\n"
message 2 "Importing: base Singularity environment\n"
zcat $SINGULARITY_libexecdir/singularity/bootstrap-scripts/environment.tar | (cd $SINGULARITY_ROOTFS; tar -xf -) || exit $?

# Try to run docker-extract
$SINGULARITY_libexecdir/singularity/bin/docker-extract >/dev/null 2>/dev/null
# Code 127 if docker-extract is missing, or missing dynamic libs
if [ $? -eq 127 ]; then
    message WARNING "docker-extract failed, missing executable or libarchive\n"
    message WARNING "Will use old layer extraction method - this does not handle whiteouts\n"
    OLD_EXTRACTION="TRUE"
fi

for i in `cat "$SINGULARITY_CONTENTS"`; do
    name=`basename "$i"`
    message 1 "Exploding layer: $name\n"
    if [ ! -z "${OLD_EXTRACTION:-}" ]; then
        zcat "$i" | (cd "$SINGULARITY_ROOTFS"; tar --exclude=dev/* -xf -) || exit $?
    else
        $SINGULARITY_libexecdir/singularity/bin/docker-extract "$i" || exit $?
    fi
    # Workaround for images where /usr/{bin,sbin,lib,lib64} is mounted read-only
    chmod -f u+rw $SINGULARITY_ROOTFS/usr/{bin,sbin,lib,lib64}
done

rm -f "$SINGULARITY_CONTENTS"
