#!/bin/bash
#
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

for i in `cat "$SINGULARITY_CONTENTS"`; do
    name=`basename "$i"`
    message 2 "Exploding layer: $name\n"
    # Settings of file privileges must be buffered
    files=$( zcat "$i" | (cd "$SINGULARITY_ROOTFS"; tar --overwrite --exclude=dev/* -xvf -)) || exit $?
    for file in $files; do
        if [ -L "$SINGULARITY_ROOTFS/$file" ]; then
            # Skipping symlinks
            true
        elif [ -f "$SINGULARITY_ROOTFS/$file" ]; then
            chmod u+rw "$SINGULARITY_ROOTFS/$file" >/dev/null 2>&1
        elif [ -d "$SINGULARITY_ROOTFS/$file" ]; then
            chmod u+rwx "$SINGULARITY_ROOTFS/${file%/}" >/dev/null 2>&1
        fi
    done
    # Workaround for images where /usr/{bin,sbin,lib,lib64} is mounted read-only
    chmod -f u+rw $SINGULARITY_ROOTFS/usr/{bin,sbin,lib,lib64}
done

rm -f "$SINGULARITY_CONTENTS"
